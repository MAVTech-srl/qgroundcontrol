/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QAbstractItemModel>
#include <QGeoCoordinate>
#include <QPointer>
#include <QQuickItem>
#include <QVariant>
#include <QVector>

class QSGNode;

class GeoZoneOverlay : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QObject* map READ map WRITE setMap NOTIFY mapChanged)
    Q_PROPERTY(QAbstractItemModel* model READ model WRITE setModel NOTIFY modelChanged)

public:
    explicit GeoZoneOverlay(QQuickItem* parent = nullptr);
    QSGNode* updatePaintNode(QSGNode* oldNode, QQuickItem::UpdatePaintNodeData* updatePaintNodeData) override;

    QObject* map() const { return _map.data(); }
    QAbstractItemModel* model() const { return _model.data(); }

public slots:
    void setMap(QObject* map);
    void setModel(QAbstractItemModel* model);
    Q_INVOKABLE void refresh();

signals:
    void mapChanged();
    void modelChanged();

private slots:
    void onModelChanged();
    void onRowsInserted(const QModelIndex& parent, int first, int last);
    void onRowsRemoved(const QModelIndex& parent, int first, int last);
    void onModelReset();

private:
    enum class TriangulationMode {
        Failed = 0,
        WithHoles,
        OuterOnly,
        FanFallback,
    };

    struct ZoneRenderData {
        QVector<QGeoCoordinate> outerRing;
        QVector<QVector<QGeoCoordinate>> holeRings;
        QVector<QPointF> triangleVertices;
        TriangulationMode triangulationMode = TriangulationMode::Failed;
        QColor color;
        double minLat = 0.0;
        double maxLat = 0.0;
        double minLon = 0.0;
        double maxLon = 0.0;
        double minMercY = 0.0;
        double maxMercY = 0.0;
        double minUnwrappedLon = 0.0;
        double maxUnwrappedLon = 0.0;
    };

    void connectModel(QAbstractItemModel* model);
    void rebuildZoneCache();
    QPointF mapPointForCoordinate(const QGeoCoordinate& coordinate) const;
    TriangulationMode triangulateZone(ZoneRenderData& zone) const;

    QPointer<QObject> _map;
    QPointer<QAbstractItemModel> _model;
    QVector<ZoneRenderData> _zoneCache;
};
