/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "clipper.h"

#include <QObject>
#include <QAbstractListModel>
#include <QGeoCoordinate>
#include <QColor>
#include <QList>
#include <QFutureWatcher>

#include <atomic>
#include <memory>

class GeoZoneManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel* model READ model CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    explicit GeoZoneManager(QObject* parent = nullptr);
    ~GeoZoneManager();

    static GeoZoneManager *instance();
    static void registerQmlTypes();

    QAbstractListModel* model();
    int count() const { return _zones.count(); }
    bool loading() const { return _loading; }

    /// Parses and clips the zones of the file on a worker thread, an empty path clears all zones.
    Q_INVOKABLE void loadFromFile(const QString& path);

signals:
    void countChanged();
    void loadingChanged();

public slots:
    void init();

private:
    // -----------------------------
    // Internal GeoZone structure
    // -----------------------------
    struct GeoZone {
        QString id;
        QString type;
        QString name;
        QList<QGeoCoordinate> polygon;
        QList<QList<QGeoCoordinate>> holes;
        double minAltitude = 0;
        double maxAltitude = 120;
        // Cached bounding box for future intersection tests
        double minLonLat[2] = {0, 0};
        double maxLonLat[2] = {0, 0};
        // Whether this zone has already been clipped by altitude layers
        bool alreadyClipped = false;
        
        // Determine color based on minAltitude
        QColor color() const {
            if (minAltitude == 0) return QColor(255, 0, 0);
            if (minAltitude == 25) return QColor(255, 165, 0);
            if (minAltitude == 45) return QColor(255, 255, 0);
            if (minAltitude == 60) return QColor(0, 255, 255);
            return QColor(255, 0, 0);
        }
    };

    using CancelFlag = std::shared_ptr<std::atomic_bool>;

    // These run on worker threads and must not touch any member state
    static QList<GeoZone> parseFile(const QString& path, const CancelFlag& cancel);
    static QList<GeoZone> clipZones(const QList<GeoZone>& zones, const CancelFlag& cancel);
    static QList<GeoZone> clipCluster(const QList<GeoZone>& zones, const CancelFlag& cancel);
    static Clipper2Lib::PathsD geoZoneToClipperPaths(const QList<QGeoCoordinate>& polygon, const QList<QList<QGeoCoordinate>>& holes = {});
    static QList<QGeoCoordinate> clipperPathToGeoZone(const Clipper2Lib::PathD& path);
    static void updateBoundingBox(GeoZone& zone);

    void _onLoadFinished();
    void _setLoading(bool loading);

    // -----------------------------
    // Model exposed to QML
    // -----------------------------
    class GeoZoneModel : public QAbstractListModel {
    public:
        enum Roles {
            PathRole   = Qt::UserRole + 1,
            ColorRole  = Qt::UserRole + 2,
            MinAltRole = Qt::UserRole + 3,
            MaxAltRole = Qt::UserRole + 4,
            HoleRole   = Qt::UserRole + 5
        };

        int rowCount(const QModelIndex& parent = QModelIndex()) const override {
            Q_UNUSED(parent)
            return _zones.count();
        }

        QVariant data(const QModelIndex& index, int role) const override {
            const GeoZone& z = _zones[index.row()];

            switch (role) {
            case PathRole: {
                QVariantList path;
                for (const auto& c : z.polygon)
                    path << QVariant::fromValue(c);
                return path;
            }
            case ColorRole:
                return z.color();
            case MinAltRole:
                return z.minAltitude;
            case MaxAltRole:
                return z.maxAltitude;
            case HoleRole: {
                QVariantList holes;
                for (const QList<QGeoCoordinate>& holeRing : z.holes) {
                    QVariantList ring;
                    for (const auto& c : holeRing) {
                        ring << QVariant::fromValue(c);
                    }
                    holes.append(QVariant(ring));
                }
                return holes;
            }
            }

            return {};
        }

        QHash<int, QByteArray> roleNames() const override {
            return {
                {PathRole,   "path"},
                {ColorRole,  "color"},
                {MinAltRole, "minAlt"},
                {MaxAltRole, "maxAlt"},
                {HoleRole,   "hole"}
            };
        }

        void setZones(const QList<GeoZone>& zones) {
            beginResetModel();
            _zones = zones;
            endResetModel();
        }

        QList<GeoZone> getZones() {
            return _zones;
        }

    private:
        QList<GeoZone> _zones;
    };

private:
    GeoZoneModel _model;
    QList<GeoZone> _zones;
    QFutureWatcher<QList<GeoZone>> _loadWatcher;
    CancelFlag _cancelLoad;
    bool _loading = false;
};
