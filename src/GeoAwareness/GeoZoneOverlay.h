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
#include <QColor>
#include <QGeoCoordinate>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QVector>
#include <QtQmlIntegration/QtQmlIntegration>

class QSGNode;

// Draws the geo zones as a filled triangle mesh on top of a map.
// The mesh is baked into a fixed pixel space and only re-baked when the view leaves the baked area
// or the zoom drifts too far from the bake zoom. Panning and zooming in between only update a matrix.
class GeoZoneOverlay : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
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

private:
    struct ZoneInput {
        QColor color;
        QVector<QGeoCoordinate> outerRing;
        QVector<QVector<QGeoCoordinate>> holeRings;
    };

    // Vertices are (unwrapped longitude in degrees, Mercator Y in radians)
    struct ZoneRenderData {
        QVector<QPointF> vertices;
        QVector<quint32> indices;
        QColor color;
        double minLon = 0.0;
        double maxLon = 0.0;
        double minMercY = 0.0;
        double maxMercY = 0.0;
    };

    void connectModel(QAbstractItemModel* model);
    void rebuildZoneCache();
    static ZoneRenderData triangulateZone(const ZoneInput& input);
    bool bakedViewContains(double zoomLevel, double offsetX, double offsetY, double mapWidth, double mapHeight) const;
    void bakeGeometry(class QSGGeometryNode* node, const QGeoCoordinate& center, double zoomLevel, double mapWidth, double mapHeight);

    QPointer<QObject> _map;
    QPointer<QAbstractItemModel> _model;
    QVector<ZoneRenderData> _zoneCache;
    bool _cacheDirty = true;

    // State of the currently baked geometry
    bool _geometryDirty = true;
    double _bakeZoom = 0.0;
    double _bakeLon = 0.0;
    double _bakeMercY = 0.0;
    QRectF _bakeRect;   // Baked area in bake pixel space
};
