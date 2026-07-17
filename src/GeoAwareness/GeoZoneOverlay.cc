/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "GeoZoneOverlay.h"
#include "earcut.hpp"

#include <cmath>
#include <QQuickItem>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QGeoCoordinate>
#include <QAbstractItemModel>
#include <QVariant>
#include <QVector>
#include <QtQml/QQmlProperty>

#include <array>
#include <vector>

namespace {

bool pointsEqual(const std::array<double, 2>& a, const std::array<double, 2>& b)
{
    return a[0] == b[0] && a[1] == b[1];
}

double mercatorYFromLatitude(double latitudeDeg)
{
    // Clamp latitude to the valid Web Mercator range to avoid infinities.
    constexpr double kMaxMercatorLat = 85.05112878;
    const double clampedLat = std::clamp(latitudeDeg, -kMaxMercatorLat, kMaxMercatorLat);
    const double latRad = clampedLat * M_PI / 180.0;
    return std::log(std::tan(M_PI / 4.0 + latRad / 2.0));
}

double normalizeDeltaLongitude(double deltaLongitude)
{
    // Project to the nearest wrapped world copy in [-180, 180].
    return std::remainder(deltaLongitude, 360.0);
}

bool intervalIntersects(double minA, double maxA, double minB, double maxB)
{
    return !(maxA < minB || minA > maxB);
}

constexpr int kOverlayAlpha = static_cast<int>(0.4f * 255.0f);
constexpr int kOutlineAlpha = 0;

} // namespace

GeoZoneOverlay::GeoZoneOverlay(QQuickItem* parent) : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::NoButton);
    setFlag(QQuickItem::ItemHasContents, true);

    connect(this, &QQuickItem::widthChanged, this, &GeoZoneOverlay::refresh);
    connect(this, &QQuickItem::heightChanged, this, &GeoZoneOverlay::refresh);
}

void GeoZoneOverlay::setMap(QObject* map)
{
    if (_map == map) {
        return;
    }

    if (_map) {
        disconnect(_map, nullptr, this, nullptr);
    }

    _map = map;

    if (_map) {
        // Repaint whenever the map view changes
        connect(_map, SIGNAL(centerChanged(QGeoCoordinate)), this, SLOT(refresh()));
        connect(_map, SIGNAL(zoomLevelChanged(qreal)),       this, SLOT(refresh()));
        connect(_map, SIGNAL(widthChanged()),                this, SLOT(refresh()));
        connect(_map, SIGNAL(heightChanged()),               this, SLOT(refresh()));
    }

    emit mapChanged();
    refresh();
}

void GeoZoneOverlay::setModel(QAbstractItemModel* model)
{
    if (_model == model) {
        return;
    }

    if (_model) {
        disconnect(_model, nullptr, this, nullptr);
    }

    _model = model;
    connectModel(model);
    rebuildZoneCache();
    emit modelChanged();
    refresh();
}

void GeoZoneOverlay::connectModel(QAbstractItemModel* model)
{
    if (!model) {
        return;
    }

    connect(model, &QAbstractItemModel::dataChanged, this, &GeoZoneOverlay::onModelChanged);
    connect(model, &QAbstractItemModel::rowsInserted, this, &GeoZoneOverlay::onRowsInserted);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &GeoZoneOverlay::onRowsRemoved);
    connect(model, &QAbstractItemModel::modelReset, this, &GeoZoneOverlay::onModelReset);
}

void GeoZoneOverlay::refresh()
{
    update();
}

void GeoZoneOverlay::onModelChanged()
{
    rebuildZoneCache();
    update();
}

void GeoZoneOverlay::onRowsInserted(const QModelIndex&, int, int)
{
    rebuildZoneCache();
    update();
}

void GeoZoneOverlay::onRowsRemoved(const QModelIndex&, int, int)
{
    rebuildZoneCache();
    update();
}

void GeoZoneOverlay::onModelReset()
{
    rebuildZoneCache();
    update();
}

void GeoZoneOverlay::rebuildZoneCache()
{
    _zoneCache.clear();

    if (!_model) {
        return;
    }

    const int zoneCount = _model->rowCount();
    _zoneCache.reserve(zoneCount);
    int triangulatedWithHoles = 0;
    int triangulatedOuterOnly = 0;
    int triangulatedFanFallback = 0;
    int triangulationFailed = 0;

    for (int row = 0; row < zoneCount; ++row) {
        const QModelIndex index = _model->index(row, 0);
        const QVariantList pathList = index.data(Qt::UserRole + 1).toList(); // PathRole
        if (pathList.size() < 3) {
            continue;
        }

        ZoneRenderData zone;
        zone.color = index.data(Qt::UserRole + 2).value<QColor>(); // ColorRole

        zone.minLat = std::numeric_limits<double>::max();
        zone.maxLat = std::numeric_limits<double>::lowest();
        zone.minLon = std::numeric_limits<double>::max();
        zone.maxLon = std::numeric_limits<double>::lowest();
        zone.minMercY = std::numeric_limits<double>::max();
        zone.maxMercY = std::numeric_limits<double>::lowest();

        zone.outerRing.reserve(pathList.size());
        for (const QVariant& coordVar : pathList) {
            const QGeoCoordinate coordinate = coordVar.value<QGeoCoordinate>();
            if (!coordinate.isValid()) {
                continue;
            }

            zone.outerRing.append(coordinate);
            zone.minLat = std::min(zone.minLat, coordinate.latitude());
            zone.maxLat = std::max(zone.maxLat, coordinate.latitude());
            zone.minLon = std::min(zone.minLon, coordinate.longitude());
            zone.maxLon = std::max(zone.maxLon, coordinate.longitude());
            const double mercY = mercatorYFromLatitude(coordinate.latitude());
            zone.minMercY = std::min(zone.minMercY, mercY);
            zone.maxMercY = std::max(zone.maxMercY, mercY);
        }

        if (zone.outerRing.size() < 3 || zone.minLat > zone.maxLat || zone.minLon > zone.maxLon) {
            continue;
        }

        {
            double previousRawLon = zone.outerRing.first().longitude();
            double currentUnwrappedLon = previousRawLon;
            zone.minUnwrappedLon = currentUnwrappedLon;
            zone.maxUnwrappedLon = currentUnwrappedLon;

            for (int i = 1; i < zone.outerRing.size(); ++i) {
                const double rawLon = zone.outerRing[i].longitude();
                const double deltaLon = normalizeDeltaLongitude(rawLon - previousRawLon);
                currentUnwrappedLon += deltaLon;
                zone.minUnwrappedLon = std::min(zone.minUnwrappedLon, currentUnwrappedLon);
                zone.maxUnwrappedLon = std::max(zone.maxUnwrappedLon, currentUnwrappedLon);
                previousRawLon = rawLon;
            }
        }

        const QVariantList holeRings = index.data(Qt::UserRole + 5).toList(); // HoleRole
        zone.holeRings.reserve(holeRings.size());
        for (const QVariant& ringVar : holeRings) {
            const QVariantList ring = ringVar.toList();
            if (ring.size() < 3) {
                continue;
            }

            QVector<QGeoCoordinate> holeRing;
            holeRing.reserve(ring.size());
            for (const QVariant& coordVar : ring) {
                const QGeoCoordinate coordinate = coordVar.value<QGeoCoordinate>();
                if (coordinate.isValid()) {
                    holeRing.append(coordinate);
                }
            }

            if (holeRing.size() >= 3) {
                zone.holeRings.append(holeRing);
            }
        }

        zone.triangulationMode = triangulateZone(zone);
        switch (zone.triangulationMode) {
        case TriangulationMode::WithHoles:
            triangulatedWithHoles++;
            break;
        case TriangulationMode::OuterOnly:
            triangulatedOuterOnly++;
            break;
        case TriangulationMode::FanFallback:
            triangulatedFanFallback++;
            break;
        case TriangulationMode::Failed:
            triangulationFailed++;
            break;
        }

        _zoneCache.append(zone);
    }
}

QPointF GeoZoneOverlay::mapPointForCoordinate(const QGeoCoordinate& coordinate) const
{
    if (!_map) {
        return QPointF();
    }

    QGeoCoordinate centerCoord = QQmlProperty::read(_map, "center").value<QGeoCoordinate>();
    double zoomLevel = QQmlProperty::read(_map, "zoomLevel").toDouble();
    double mapWidth  = QQmlProperty::read(_map, "width").toDouble();
    double mapHeight = QQmlProperty::read(_map, "height").toDouble();

    // Web Mercator (EPSG:3857): total world pixels at this zoom level
    const double totalPixels = 256.0 * std::pow(2.0, zoomLevel);

    // Gudermannian inverse — projects latitude to Mercator Y in radians
    auto mercY = [](double lat) -> double {
        const double latRad = lat * M_PI / 180.0;
        return std::log(std::tan(M_PI / 4.0 + latRad / 2.0));
    };

    // Longitude → screen X: 360° maps to totalPixels, origin at center
    const double screenX = mapWidth  / 2.0
        + (coordinate.longitude() - centerCoord.longitude()) * (totalPixels / 360.0);

    // Latitude → screen Y: mercY range (-π, π) maps to totalPixels, Y axis inverted
    const double screenY = mapHeight / 2.0
        + (mercY(centerCoord.latitude()) - mercY(coordinate.latitude())) * (totalPixels / (2.0 * M_PI));

    return QPointF(screenX, screenY);
}

GeoZoneOverlay::TriangulationMode GeoZoneOverlay::triangulateZone(ZoneRenderData& zone) const
{
    zone.triangleVertices.clear();

    std::vector<std::vector<std::array<double, 2>>> polygon;
    polygon.reserve(1 + static_cast<size_t>(zone.holeRings.size()));

    std::vector<std::array<double, 2>> flattened;
    flattened.reserve(static_cast<size_t>(zone.outerRing.size()));

    auto appendRingPoints = [&polygon, &flattened](const QVector<QGeoCoordinate>& ring) {
        if (ring.size() < 3) {
            return;
        }

        std::vector<std::array<double, 2>> ringPoints;
        ringPoints.reserve(static_cast<size_t>(ring.size()));

        double previousRawLon = ring.first().longitude();
        double currentUnwrappedLon = previousRawLon;
        ringPoints.push_back({ currentUnwrappedLon, mercatorYFromLatitude(ring.first().latitude()) });

        for (int i = 1; i < ring.size(); ++i) {
            const double rawLon = ring[i].longitude();
            const double deltaLon = normalizeDeltaLongitude(rawLon - previousRawLon);
            currentUnwrappedLon += deltaLon;
            ringPoints.push_back({ currentUnwrappedLon, mercatorYFromLatitude(ring[i].latitude()) });
            previousRawLon = rawLon;
        }

        while (ringPoints.size() > 1 && pointsEqual(ringPoints.front(), ringPoints.back())) {
            ringPoints.pop_back();
        }

        if (ringPoints.size() < 3) {
            return;
        }

        polygon.push_back(ringPoints);
        flattened.insert(flattened.end(), ringPoints.begin(), ringPoints.end());
    };

    appendRingPoints(zone.outerRing);
    if (polygon.empty()) {
        return TriangulationMode::Failed;
    }

    for (const QVector<QGeoCoordinate>& holeRing : zone.holeRings) {
        appendRingPoints(holeRing);
    }

    std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(polygon);
    TriangulationMode triangulationMode = TriangulationMode::WithHoles;

    if (indices.empty() && polygon.front().size() >= 3) {
        // Hole topology can occasionally be invalid after clipping; render outer shell as fallback.
        flattened.assign(polygon.front().begin(), polygon.front().end());
        const uint32_t vertexCount = static_cast<uint32_t>(flattened.size());
        indices.reserve(static_cast<size_t>((vertexCount - 2) * 3));
        for (uint32_t i = 1; i + 1 < vertexCount; ++i) {
            indices.push_back(0);
            indices.push_back(i);
            indices.push_back(i + 1);
        }
        triangulationMode = indices.empty() ? TriangulationMode::Failed : TriangulationMode::FanFallback;
    }

    if (indices.empty()) {
        return TriangulationMode::Failed;
    }

    zone.triangleVertices.reserve(static_cast<int>(indices.size()));
    for (const uint32_t index : indices) {
        if (index < flattened.size()) {
            const std::array<double, 2>& point = flattened[index];
            zone.triangleVertices.append(QPointF(point[0], point[1]));
        }
    }

    return zone.triangleVertices.isEmpty() ? TriangulationMode::Failed : triangulationMode;
}

QSGNode* GeoZoneOverlay::updatePaintNode(QSGNode* oldNode, QQuickItem::UpdatePaintNodeData* /*updatePaintNodeData*/)
{
    if (!_map || !_model) {
        delete oldNode;
        return nullptr;
    }

    QSGNode* rootNode = oldNode ? oldNode : new QSGNode;
    QSGGeometryNode* fillGeometryNode = static_cast<QSGGeometryNode*>(rootNode->childAtIndex(0));
    QSGGeometryNode* outlineGeometryNode = static_cast<QSGGeometryNode*>(rootNode->childAtIndex(1));
    if (!fillGeometryNode || !outlineGeometryNode) {
        while (QSGNode* child = rootNode->firstChild()) {
            rootNode->removeChildNode(child);
            delete child;
        }

        fillGeometryNode = new QSGGeometryNode;
        QSGGeometry* fillGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        fillGeometry->setDrawingMode(QSGGeometry::DrawTriangles);

        QSGVertexColorMaterial* fillMaterial = new QSGVertexColorMaterial;
        fillMaterial->setFlag(QSGMaterial::Blending, true);
        fillMaterial->setFlag(QSGMaterial::RequiresFullMatrix, true);

        fillGeometryNode->setFlag(QSGNode::OwnsGeometry);
        fillGeometryNode->setFlag(QSGNode::OwnsMaterial);
        fillGeometryNode->setGeometry(fillGeometry);
        fillGeometryNode->setMaterial(fillMaterial);
        rootNode->appendChildNode(fillGeometryNode);

        outlineGeometryNode = new QSGGeometryNode;
        QSGGeometry* outlineGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        outlineGeometry->setDrawingMode(QSGGeometry::DrawLines);

        QColor outlineColor(0, 0, 0, kOutlineAlpha);
        QSGFlatColorMaterial* outlineMaterial = new QSGFlatColorMaterial;
        outlineMaterial->setColor(outlineColor);
        outlineMaterial->setFlag(QSGMaterial::Blending, true);

        outlineGeometryNode->setFlag(QSGNode::OwnsGeometry);
        outlineGeometryNode->setFlag(QSGNode::OwnsMaterial);
        outlineGeometryNode->setGeometry(outlineGeometry);
        outlineGeometryNode->setMaterial(outlineMaterial);
        rootNode->appendChildNode(outlineGeometryNode);
    }

    if (_zoneCache.isEmpty()) {
        fillGeometryNode->geometry()->allocate(0);
        outlineGeometryNode->geometry()->allocate(0);
        fillGeometryNode->markDirty(QSGNode::DirtyGeometry);
        outlineGeometryNode->markDirty(QSGNode::DirtyGeometry);
        return rootNode;
    }

    int totalFillVertexCount = 0;
    int totalOutlineVertexCount = 0;
    int visibleZones = 0;
    int culledZones = 0;
    int visibleZonesNoTriangles = 0;
    int renderedZones = 0;
    for (const ZoneRenderData& zone : _zoneCache) {
        if (zone.triangleVertices.isEmpty()) {
            visibleZonesNoTriangles++;
            continue;
        }

        visibleZones++;
        renderedZones++;
        totalFillVertexCount += zone.triangleVertices.size();

        totalOutlineVertexCount += zone.outerRing.size() * 2;
        for (const QVector<QGeoCoordinate>& holeRing : zone.holeRings) {
            totalOutlineVertexCount += holeRing.size() * 2;
        }
    }

    QSGGeometry* fillGeometry = fillGeometryNode->geometry();
    fillGeometry->allocate(totalFillVertexCount);

    QSGGeometry* outlineGeometry = outlineGeometryNode->geometry();
    outlineGeometry->allocate(totalOutlineVertexCount);

    if (totalFillVertexCount == 0) {
        fillGeometryNode->markDirty(QSGNode::DirtyGeometry);
        outlineGeometryNode->markDirty(QSGNode::DirtyGeometry);
        return rootNode;
    }

    QSGGeometry::ColoredPoint2D* fillVertices = fillGeometry->vertexDataAsColoredPoint2D();
    QSGGeometry::Point2D* outlineVertices = outlineGeometry->vertexDataAsPoint2D();
    int fillVertexIndex = 0;
    int outlineVertexIndex = 0;

    const QGeoCoordinate centerCoord = QQmlProperty::read(_map, "center").value<QGeoCoordinate>();
    const double zoomLevel = QQmlProperty::read(_map, "zoomLevel").toDouble();
    const double mapWidth  = QQmlProperty::read(_map, "width").toDouble();
    const double mapHeight = QQmlProperty::read(_map, "height").toDouble();
    const double totalPixels = 256.0 * std::pow(2.0, zoomLevel);
    const double xScale = totalPixels / 360.0;
    const double yScale = totalPixels / (2.0 * M_PI);
    const double centerLongitude = centerCoord.longitude();
    const double centerMercY = mercatorYFromLatitude(centerCoord.latitude());
    int renderedZonesOnScreen = 0;

    for (const ZoneRenderData& zone : _zoneCache) {
        if (zone.triangleVertices.isEmpty()) {
            continue;
        }

        const QColor fillColor = zone.color;

        bool zoneTouchesScreen = false;

        for (const QPointF& mercatorPoint : zone.triangleVertices) {
            const double deltaLon = mercatorPoint.x() - centerLongitude;
            const double screenX = mapWidth / 2.0 + deltaLon * xScale;
            const double screenY = mapHeight / 2.0 + (centerMercY - mercatorPoint.y()) * yScale;
            const int r = fillColor.red()   * kOverlayAlpha / 255;
            const int g = fillColor.green() * kOverlayAlpha / 255;
            const int b = fillColor.blue()  * kOverlayAlpha / 255;
            fillVertices[fillVertexIndex++].set(screenX, screenY, r, g, b, kOverlayAlpha);

            if (!zoneTouchesScreen && screenX >= 0.0 && screenX <= mapWidth && screenY >= 0.0 && screenY <= mapHeight) {
                zoneTouchesScreen = true;
            }
        }

        auto appendOutlineRing = [&](const QVector<QGeoCoordinate>& ring) {
            if (kOutlineAlpha == 0) {
                return;
            }
            const int ringSize = ring.size();
            if (ringSize < 2) {
                return;
            }

            double previousRawLon = ring.first().longitude();
            double currentUnwrappedLon = centerLongitude + normalizeDeltaLongitude(previousRawLon - centerLongitude);
            double previousX = mapWidth / 2.0 + (currentUnwrappedLon - centerLongitude) * xScale;
            double previousY = mapHeight / 2.0 + (centerMercY - mercatorYFromLatitude(ring.first().latitude())) * yScale;

            for (int i = 1; i <= ringSize; ++i) {
                const QGeoCoordinate& b = ring[i % ringSize];
                const double rawLon = b.longitude();
                const double deltaLon = normalizeDeltaLongitude(rawLon - previousRawLon);
                currentUnwrappedLon += deltaLon;

                const double bx = mapWidth / 2.0 + (currentUnwrappedLon - centerLongitude) * xScale;
                const double by = mapHeight / 2.0 + (centerMercY - mercatorYFromLatitude(b.latitude())) * yScale;

                outlineVertices[outlineVertexIndex++].set(previousX, previousY);
                outlineVertices[outlineVertexIndex++].set(bx, by);

                previousRawLon = rawLon;
                previousX = bx;
                previousY = by;
            }
        };

        appendOutlineRing(zone.outerRing);
        for (const QVector<QGeoCoordinate>& holeRing : zone.holeRings) {
            appendOutlineRing(holeRing);
        }

        if (zoneTouchesScreen) {
            renderedZonesOnScreen++;
        }
    }

    fillGeometryNode->markDirty(QSGNode::DirtyGeometry);
    outlineGeometryNode->markDirty(QSGNode::DirtyGeometry);

    static int lastTotalZones = -1;
    static int lastVisibleZones = -1;
    static int lastCulledZones = -1;
    static int lastVisibleNoTriangles = -1;
    static int lastRenderedZones = -1;
    static int lastFillVertexCount = -1;
    static int lastOutlineVertexCount = -1;
    static int lastRenderedZonesOnScreen = -1;

    const int totalZones = _zoneCache.size();
    if (totalZones != lastTotalZones ||
        visibleZones != lastVisibleZones ||
        culledZones != lastCulledZones ||
        visibleZonesNoTriangles != lastVisibleNoTriangles ||
        renderedZones != lastRenderedZones ||
        totalFillVertexCount != lastFillVertexCount ||
        totalOutlineVertexCount != lastOutlineVertexCount ||
        renderedZonesOnScreen != lastRenderedZonesOnScreen) {
        lastTotalZones = totalZones;
        lastVisibleZones = visibleZones;
        lastCulledZones = culledZones;
        lastVisibleNoTriangles = visibleZonesNoTriangles;
        lastRenderedZones = renderedZones;
        lastFillVertexCount = totalFillVertexCount;
        lastOutlineVertexCount = totalOutlineVertexCount;
        lastRenderedZonesOnScreen = renderedZonesOnScreen;
    }

    return rootNode;
}
