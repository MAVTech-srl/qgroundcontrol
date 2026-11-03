/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "GeoZoneOverlay.h"
#include <mapbox/earcut.hpp>

#include <cmath>
#include <QQuickItem>
#include <QMatrix4x4>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>
#include <QVariant>
#include <QtConcurrent>
#include <QtQml/QQmlProperty>

#include <algorithm>
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

// Pixels per degree of longitude / per radian of Mercator Y at a zoom level
double xScaleForZoom(double zoomLevel) { return 256.0 * std::pow(2.0, zoomLevel) / 360.0; }
double yScaleForZoom(double zoomLevel) { return 256.0 * std::pow(2.0, zoomLevel) / (2.0 * M_PI); }

constexpr int kOverlayAlpha = static_cast<int>(0.4f * 255.0f);

// The baked mesh is re-created when the zoom differs by more than this from the bake zoom
constexpr double kMaxZoomDrift = 1.0;

} // namespace

GeoZoneOverlay::GeoZoneOverlay(QQuickItem* parent) : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::NoButton);
    setFlag(QQuickItem::ItemHasContents, true);

    connect(this, &QQuickItem::widthChanged, this, &GeoZoneOverlay::refresh);
    connect(this, &QQuickItem::heightChanged, this, &GeoZoneOverlay::refresh);
    connect(this, &QQuickItem::visibleChanged, this, &GeoZoneOverlay::refresh);
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
    onModelChanged();
    emit modelChanged();
}

void GeoZoneOverlay::connectModel(QAbstractItemModel* model)
{
    if (!model) {
        return;
    }

    connect(model, &QAbstractItemModel::dataChanged,  this, &GeoZoneOverlay::onModelChanged);
    connect(model, &QAbstractItemModel::rowsInserted, this, &GeoZoneOverlay::onModelChanged);
    connect(model, &QAbstractItemModel::rowsRemoved,  this, &GeoZoneOverlay::onModelChanged);
    connect(model, &QAbstractItemModel::modelReset,   this, &GeoZoneOverlay::onModelChanged);
}

void GeoZoneOverlay::refresh()
{
    // A hidden overlay never gets painted, so there is nothing to keep up to date
    if (isVisible()) {
        update();
    }
}

void GeoZoneOverlay::onModelChanged()
{
    // The cache is rebuilt lazily on the next paint, which also coalesces bursts of model signals
    _cacheDirty = true;
    _geometryDirty = true;
    refresh();
}

void GeoZoneOverlay::rebuildZoneCache()
{
    _cacheDirty = false;
    _zoneCache.clear();

    if (!_model) {
        return;
    }

    const int zoneCount = _model->rowCount();
    QVector<ZoneInput> inputs;
    inputs.reserve(zoneCount);

    for (int row = 0; row < zoneCount; ++row) {
        const QModelIndex index = _model->index(row, 0);
        const QVariantList pathList = index.data(Qt::UserRole + 1).toList(); // PathRole
        if (pathList.size() < 3) {
            continue;
        }

        ZoneInput input;
        input.color = index.data(Qt::UserRole + 2).value<QColor>(); // ColorRole

        input.outerRing.reserve(pathList.size());
        for (const QVariant& coordVar : pathList) {
            const QGeoCoordinate coordinate = coordVar.value<QGeoCoordinate>();
            if (coordinate.isValid()) {
                input.outerRing.append(coordinate);
            }
        }

        if (input.outerRing.size() < 3) {
            continue;
        }

        const QVariantList holeRings = index.data(Qt::UserRole + 5).toList(); // HoleRole
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
                input.holeRings.append(holeRing);
            }
        }

        inputs.append(input);
    }

    // Model access is done, triangulation is pure computation and can run in parallel
    const QList<ZoneRenderData> zones = QtConcurrent::blockingMapped(inputs, [](const ZoneInput& input) {
        return triangulateZone(input);
    });

    _zoneCache.reserve(zones.size());
    for (const ZoneRenderData& zone : zones) {
        if (!zone.indices.isEmpty()) {
            _zoneCache.append(zone);
        }
    }
}

GeoZoneOverlay::ZoneRenderData GeoZoneOverlay::triangulateZone(const ZoneInput& input)
{
    ZoneRenderData zone;
    zone.color = input.color;

    std::vector<std::vector<std::array<double, 2>>> polygon;
    polygon.reserve(1 + static_cast<size_t>(input.holeRings.size()));

    std::vector<std::array<double, 2>> flattened;

    auto appendRingPoints = [&polygon, &flattened](const QVector<QGeoCoordinate>& ring) {
        if (ring.size() < 3) {
            return;
        }

        std::vector<std::array<double, 2>> ringPoints;
        ringPoints.reserve(static_cast<size_t>(ring.size()));

        // Unwrap longitudes so rings crossing the antimeridian stay contiguous
        double previousRawLon = ring.first().longitude();
        double currentUnwrappedLon = previousRawLon;
        ringPoints.push_back({ currentUnwrappedLon, mercatorYFromLatitude(ring.first().latitude()) });

        for (int i = 1; i < ring.size(); ++i) {
            const double rawLon = ring[i].longitude();
            currentUnwrappedLon += normalizeDeltaLongitude(rawLon - previousRawLon);
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

    appendRingPoints(input.outerRing);
    if (polygon.empty()) {
        return zone;
    }

    for (const QVector<QGeoCoordinate>& holeRing : input.holeRings) {
        appendRingPoints(holeRing);
    }

    std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(polygon);

    if (indices.empty()) {
        // Hole topology can occasionally be invalid after clipping; render outer shell as fallback.
        flattened.assign(polygon.front().begin(), polygon.front().end());
        const uint32_t vertexCount = static_cast<uint32_t>(flattened.size());
        for (uint32_t i = 1; i + 1 < vertexCount; ++i) {
            indices.push_back(0);
            indices.push_back(i);
            indices.push_back(i + 1);
        }
    }

    if (indices.empty()) {
        return zone;
    }

    zone.minLon = zone.minMercY = std::numeric_limits<double>::max();
    zone.maxLon = zone.maxMercY = std::numeric_limits<double>::lowest();
    zone.vertices.reserve(static_cast<int>(flattened.size()));
    for (const std::array<double, 2>& point : flattened) {
        zone.vertices.append(QPointF(point[0], point[1]));
        zone.minLon = std::min(zone.minLon, point[0]);
        zone.maxLon = std::max(zone.maxLon, point[0]);
        zone.minMercY = std::min(zone.minMercY, point[1]);
        zone.maxMercY = std::max(zone.maxMercY, point[1]);
    }

    zone.indices.reserve(static_cast<int>(indices.size()));
    for (const uint32_t index : indices) {
        zone.indices.append(index);
    }

    return zone;
}

bool GeoZoneOverlay::bakedViewContains(double zoomLevel, double offsetX, double offsetY, double mapWidth, double mapHeight) const
{
    if (std::abs(zoomLevel - _bakeZoom) > kMaxZoomDrift) {
        return false;
    }

    // Visible area expressed in bake pixel space
    const double scale = std::pow(2.0, zoomLevel - _bakeZoom);
    const QRectF view(-mapWidth / 2.0 / scale - offsetX, -mapHeight / 2.0 / scale - offsetY,
                      mapWidth / scale, mapHeight / scale);
    return _bakeRect.contains(view);
}

void GeoZoneOverlay::bakeGeometry(QSGGeometryNode* node, const QGeoCoordinate& center, double zoomLevel, double mapWidth, double mapHeight)
{
    _bakeZoom = zoomLevel;
    _bakeLon = center.longitude();
    _bakeMercY = mercatorYFromLatitude(center.latitude());

    const double xScale = xScaleForZoom(_bakeZoom);
    const double yScale = yScaleForZoom(_bakeZoom);

    // Bake the visible area plus one screen of margin on every side so that panning does not trigger a re-bake right away
    const double margin = std::max(mapWidth, mapHeight);
    _bakeRect = QRectF(-mapWidth / 2.0 - margin, -mapHeight / 2.0 - margin, mapWidth + 2.0 * margin, mapHeight + 2.0 * margin);

    const double minLon = _bakeLon + _bakeRect.left() / xScale;
    const double maxLon = _bakeLon + _bakeRect.right() / xScale;
    const double maxMercY = _bakeMercY - _bakeRect.top() / yScale;
    const double minMercY = _bakeMercY - _bakeRect.bottom() / yScale;

    QVector<const ZoneRenderData*> zones;
    int vertexCount = 0;
    int indexCount = 0;
    for (const ZoneRenderData& zone : std::as_const(_zoneCache)) {
        if (zone.maxLon < minLon || zone.minLon > maxLon || zone.maxMercY < minMercY || zone.minMercY > maxMercY) {
            continue;
        }

        zones.append(&zone);
        vertexCount += zone.vertices.size();
        indexCount += zone.indices.size();
    }

    const int indexType = vertexCount <= std::numeric_limits<quint16>::max() ? QSGGeometry::UnsignedShortType : QSGGeometry::UnsignedIntType;
    QSGGeometry* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), vertexCount, indexCount, indexType);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);

    QSGGeometry::ColoredPoint2D* vertices = geometry->vertexDataAsColoredPoint2D();
    quint16* shortIndices = indexType == QSGGeometry::UnsignedShortType ? geometry->indexDataAsUShort() : nullptr;
    quint32* intIndices = indexType == QSGGeometry::UnsignedIntType ? geometry->indexDataAsUInt() : nullptr;

    int vertexOffset = 0;
    int indexOffset = 0;
    for (const ZoneRenderData* zone : std::as_const(zones)) {
        // Premultiplied alpha
        const int r = zone->color.red()   * kOverlayAlpha / 255;
        const int g = zone->color.green() * kOverlayAlpha / 255;
        const int b = zone->color.blue()  * kOverlayAlpha / 255;

        for (const QPointF& point : zone->vertices) {
            vertices[vertexOffset++].set((point.x() - _bakeLon) * xScale, (_bakeMercY - point.y()) * yScale, r, g, b, kOverlayAlpha);
        }

        const quint32 base = static_cast<quint32>(vertexOffset - zone->vertices.size());
        for (const quint32 index : zone->indices) {
            if (shortIndices) {
                shortIndices[indexOffset++] = static_cast<quint16>(base + index);
            } else {
                intIndices[indexOffset++] = base + index;
            }
        }
    }

    // The node owns its geometry and frees the previous one itself
    node->setGeometry(geometry);
    node->markDirty(QSGNode::DirtyGeometry);
    _geometryDirty = false;
}

QSGNode* GeoZoneOverlay::updatePaintNode(QSGNode* oldNode, QQuickItem::UpdatePaintNodeData*)
{
    if (!_map || !_model) {
        delete oldNode;
        _geometryDirty = true;
        return nullptr;
    }

    const QGeoCoordinate center = QQmlProperty::read(_map, "center").value<QGeoCoordinate>();
    const double zoomLevel = QQmlProperty::read(_map, "zoomLevel").toDouble();
    const double mapWidth  = QQmlProperty::read(_map, "width").toDouble();
    const double mapHeight = QQmlProperty::read(_map, "height").toDouble();

    QSGTransformNode* transformNode = static_cast<QSGTransformNode*>(oldNode);
    QSGGeometryNode* fillNode = nullptr;
    if (transformNode) {
        fillNode = static_cast<QSGGeometryNode*>(transformNode->firstChild());
    } else {
        transformNode = new QSGTransformNode;

        fillNode = new QSGGeometryNode;
        QSGVertexColorMaterial* material = new QSGVertexColorMaterial;
        material->setFlag(QSGMaterial::Blending, true);
        // Keeps the renderer from baking the pan/zoom matrix into the vertices on the CPU
        material->setFlag(QSGMaterial::RequiresFullMatrix, true);

        fillNode->setFlag(QSGNode::OwnsGeometry);
        fillNode->setFlag(QSGNode::OwnsMaterial);
        fillNode->setGeometry(new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0));
        fillNode->setMaterial(material);
        transformNode->appendChildNode(fillNode);
        _geometryDirty = true;
    }

    if (!center.isValid() || mapWidth <= 0.0 || mapHeight <= 0.0) {
        return transformNode;
    }

    if (_cacheDirty) {
        rebuildZoneCache();
        _geometryDirty = true;
    }

    const double centerMercY = mercatorYFromLatitude(center.latitude());

    // Offset of the current center relative to the bake center, in bake pixel space
    double offsetX = (_bakeLon - center.longitude()) * xScaleForZoom(_bakeZoom);
    double offsetY = (centerMercY - _bakeMercY) * yScaleForZoom(_bakeZoom);

    if (_geometryDirty || !bakedViewContains(zoomLevel, offsetX, offsetY, mapWidth, mapHeight)) {
        bakeGeometry(fillNode, center, zoomLevel, mapWidth, mapHeight);
        offsetX = 0.0;
        offsetY = 0.0;
    }

    // screen = mapSize / 2 + scale * (bakedVertex + offset)
    const double scale = std::pow(2.0, zoomLevel - _bakeZoom);
    QMatrix4x4 matrix;
    matrix.translate(static_cast<float>(mapWidth / 2.0), static_cast<float>(mapHeight / 2.0));
    matrix.scale(static_cast<float>(scale), static_cast<float>(scale));
    matrix.translate(static_cast<float>(offsetX), static_cast<float>(offsetY));
    transformNode->setMatrix(matrix);

    return transformNode;
}
