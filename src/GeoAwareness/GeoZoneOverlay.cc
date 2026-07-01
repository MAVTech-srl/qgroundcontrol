/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "GeoZoneOverlay.h"

#include <cmath>
#include <QDebug>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QQuickItem>
#include <QQuickWindow>
#include <QGeoCoordinate>
#include <QAbstractItemModel>
#include <QVariant>
#include <QtQml/QQmlProperty>

GeoZoneOverlay::GeoZoneOverlay(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::NoButton);
    setAntialiasing(true);
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
    update();
}

void GeoZoneOverlay::onRowsInserted(const QModelIndex&, int, int)
{
    update();
}

void GeoZoneOverlay::onRowsRemoved(const QModelIndex&, int, int)
{
    update();
}

void GeoZoneOverlay::onModelReset()
{
    update();
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

void GeoZoneOverlay::paint(QPainter* painter)
{
    if (!_map || !_model) {
        return;
    }

    const int zoneCount = _model->rowCount();
    if (zoneCount == 0) {
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);

    for (int row = 0; row < zoneCount; ++row) {
        QModelIndex index = _model->index(row, 0);
        QVariant colorVar = index.data(Qt::UserRole + 2); // ColorRole
        QColor fillColor = colorVar.value<QColor>();
        fillColor.setAlphaF(0.4);

        QVariant pathVar = index.data(Qt::UserRole + 1); // PathRole
        QVariantList pathList = pathVar.toList();
        if (pathList.isEmpty()) {
            continue;
        }

        QPainterPath polygonPath;
        polygonPath.setFillRule(Qt::OddEvenFill); // OddEvenFill cuts holes automatically

        // Outer ring
        bool firstPoint = true;
        for (const QVariant& coordVar : pathList) {
            QPointF screenPoint = mapPointForCoordinate(coordVar.value<QGeoCoordinate>());
            if (firstPoint) {
                polygonPath.moveTo(screenPoint);
                firstPoint = false;
            } else {
                polygonPath.lineTo(screenPoint);
            }
        }
        polygonPath.closeSubpath();

        if (polygonPath.isEmpty()) {
            continue;
        }

        // Hole ring (if present) — OddEvenFill will subtract it
        const QVariantList holeList = index.data(Qt::UserRole + 5).toList(); // HoleRole
        if (!holeList.isEmpty()) {
            bool firstHole = true;
            for (const QVariant& coordVar : holeList) {
                QPointF screenPoint = mapPointForCoordinate(coordVar.value<QGeoCoordinate>());
                if (firstHole) {
                    polygonPath.moveTo(screenPoint);
                    firstHole = false;
                } else {
                    polygonPath.lineTo(screenPoint);
                }
            }
            polygonPath.closeSubpath();
        }

        painter->fillPath(polygonPath, fillColor);
    }
}
