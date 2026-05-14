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
#include "RTree.h"

#include <QObject>
#include <QAbstractListModel>
#include <QGeoCoordinate>
#include <QColor>
#include <QList>

class GeoZoneManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel* model READ model CONSTANT)

public:
    explicit GeoZoneManager(QObject* parent = nullptr);
    ~GeoZoneManager();

    static GeoZoneManager *instance();
    static void registerQmlTypes();

    QAbstractListModel* model();

    Q_INVOKABLE void loadFromFile(const QString& path);
    Q_INVOKABLE void updateViewport(
        double topLat,
        double leftLon,
        double bottomLat,
        double rightLon
    );

private:
    // -----------------------------
    // Internal GeoZone structure
    // -----------------------------
    struct GeoZone {
        QString id;
        QString type;
        QList<QGeoCoordinate> polygon;
        double minAltitude = 0;
        double maxAltitude = 120;
        // Cached bounding box for future intersection tests
        double minLonLat[2];
        double maxLonLat[2];
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

    Clipper2Lib::PathD geoZoneToClipperPath(const QList<QGeoCoordinate>& polygon);
    QList<QGeoCoordinate> clipperPathToGeoZone(const Clipper2Lib::PathD& path);
    void insertZoneIntoTree(GeoZone& zone, int index);

    // -----------------------------
    // Model exposed to QML
    // -----------------------------
    class GeoZoneModel : public QAbstractListModel {
    public:
        enum Roles {
            PathRole = Qt::UserRole + 1,
            ColorRole,
            MinAltRole,
            MaxAltRole
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
            }

            return {};
        }

        QHash<int, QByteArray> roleNames() const override {
            return {
                {PathRole, "path"},
                {ColorRole, "color"},
                {MinAltRole, "minAlt"},
                {MaxAltRole, "maxAlt"}
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
    int displayedZone = 0;
    RTree<int, double, 2> _zoneTree;
};
