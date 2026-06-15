/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "GeoZoneManager.h"
#include "clipper.h"
#include "RTree.h"

#include "SettingsManager.h"
#include "FlightMapSettings.h"
#include <QtCore/qapplicationstatic.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <QtQml/qqml.h>

#define GEOZONE_CLIPPER_PRECISION 8

Q_APPLICATION_STATIC(GeoZoneManager, _geoZoneManager);

GeoZoneManager::GeoZoneManager(QObject* parent): QObject(parent)
{
}

GeoZoneManager::~GeoZoneManager()
{
}

QAbstractListModel* GeoZoneManager::model()
{
    return &_model;
}

GeoZoneManager *GeoZoneManager::instance()
{
    return _geoZoneManager();
}

void GeoZoneManager::registerQmlTypes()
{
    (void) qmlRegisterUncreatableType<GeoZoneManager>("QGroundControl.GeoZoneManager", 1, 0, "GeoZoneManager", "Reference only");
}

Clipper2Lib::PathD GeoZoneManager::geoZoneToClipperPath(const QList<QGeoCoordinate>& polygon)
{
    Clipper2Lib::PathD result;
    result.reserve(polygon.size());
    for (const QGeoCoordinate& coord : polygon) {
        result.emplace_back(coord.longitude(), coord.latitude());
    }
    return result;
}

QList<QGeoCoordinate> GeoZoneManager::clipperPathToGeoZone(const Clipper2Lib::PathD& path)
{
    QList<QGeoCoordinate> result;
    result.reserve(path.size());
    for (const auto& pt : path) {
        result.append(QGeoCoordinate(pt.y, pt.x));
    }
    return result;
}

void GeoZoneManager::loadFromFile(const QString& path)
{
    // Clear existing zones
    _model.setZones({});
    _zoneTree.RemoveAll();

    // Load file
    QFile file(path);
    qDebug() << "Loading file " << path;
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open GeoZone file: " << path;
        return;
    }

    // Get "features" array
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonArray features = doc.object()["features"].toArray();
    qDebug() << "Loaded " << features.size() << " features from file";

    QList<GeoZone> zones;

    // Iterate features and convert to GeoZones
    int index = 0;
    for (const auto& fVal : features) {
        QJsonObject fObj = fVal.toObject();

        QString id = fObj["identifier"].toString();
        QString name = fObj["name"].toString();

        QJsonArray geometryArray = fObj["geometry"].toArray();
        if (geometryArray.isEmpty()) {
            continue;
        }

        // Some zones are split over multiple geometries (separated by higher priority zones), we need to add each geometry as its own zone and type
        QJsonObject zoneGeometry;
        for (const auto& geometry : geometryArray) {
            zoneGeometry = geometry.toObject();
            GeoZone zone;
            zone.id = id;
            zone.name = name;

            // Altitude
            zone.minAltitude = zoneGeometry["lowerLimit"].toDouble();
            zone.maxAltitude = zoneGeometry["upperLimit"].toDouble();

            // Horizontal projection
            QJsonObject proj = zoneGeometry["horizontalProjection"].toObject();
            QJsonArray coordinateLists = proj["coordinates"].toArray();

            if (coordinateLists.isEmpty()) {
                continue;
            }

            // Handle first polygon of the geometry object as the Polygon, the second (if present) as the Hole
            for (int i = 0; i < coordinateLists.size(); i++) {
                QJsonArray coordList = coordinateLists[i].toArray();
                QList<QGeoCoordinate> geoCoordinates;
                for (const auto& coord : coordList) {
                    QJsonArray point = coord.toArray();
                    if (point.size() < 2) {
                        continue;
                    }
                    double lon = point[0].toDouble();
                    double lat = point[1].toDouble();
                    geoCoordinates.append(QGeoCoordinate(lat, lon));
                }

                if (i == 0) {
                    zone.polygon = geoCoordinates;
                } else {
                    zone.hole = geoCoordinates;
                }
            }

            // Skip invalid polygons
            if (zone.polygon.size() < 3) {
                continue;
            }

            //qDebug() << "Parsed GeoZone: index " << index << ", ID: " << zone.id << ", type: " << zone.type << ", minAlt: " << zone.minAltitude << ", polygon points: " << zone.polygon.size();
            insertZoneIntoTree(zone, index++);
            zones.append(zone);
        }
    }

    // Sort and intersect zones with R-Tree

    // Sort zones by minAltitude ascending
    /*std::sort(zones.begin(), zones.end(), [](const GeoZone& a, const GeoZone& b) {
        return a.minAltitude < b.minAltitude;
    });
    qDebug() << "Sorted GeoZones by minAltitude";

    // Clip zones based on altitude layers
    Clipper2Lib::PathsD union_lower;
    QList<GeoZone> clippedZones;
    for (const GeoZone& zone : zones) {
        Clipper2Lib::PathsD zone_paths = { geoZoneToClipperPath(zone.polygon) };
        Clipper2Lib::PathsD clipped = Clipper2Lib::Difference(zone_paths, union_lower, Clipper2Lib::FillRule::EvenOdd);
        if (!clipped.empty()) {
            GeoZone new_zone = zone;
            new_zone.polygon = clipperPathToGeoZone(clipped[0]);  // Assuming one path
            clippedZones.append(new_zone);
            union_lower = Clipper2Lib::Union(union_lower, clipped, Clipper2Lib::FillRule::EvenOdd);
        }
    }
    zones = clippedZones;

    // Rebuild R-tree with clipped zones
    _zoneTree.RemoveAll();
    for (int i = 0; i < zones.size(); ++i) {
        insertZoneIntoTree(zones[i], i);
    }*/

    // Tree iterator
    /*auto list = _zoneTree.ListTree();
    int counter = 0;
    for (auto aabb : list) {
        qDebug() << "TreeList [" << counter++ << "]: "
            << aabb.m_min[0] << ", "
            << aabb.m_min[1] << ", "
            << aabb.m_min[2] << "; "
            << aabb.m_max[0] << ", "
            << aabb.m_max[1] << ", "
            << aabb.m_max[2];
    }*/

    qDebug() << "Loaded GeoZones: " << zones.size();

    _model.setZones(zones);
    _zones = zones; // Store zones for later viewport queries
}

void GeoZoneManager::updateViewport(double topLat, double leftLon, double bottomLat, double rightLon)
{
    qDebug() << "Updating viewport: " << topLat << leftLon << bottomLat << rightLon;
    // Query R-tree for zones intersecting the viewport
    double min[2] = { leftLon, bottomLat };
    double max[2] = { rightLon, topLat };

    /*
     * TODO: for every GeoZone in the viewport, find any polygon intersecting with it and cut them depending on priority, then flag the afflicted zone so that it is not re-handled another time.
     * Consider caching clipped zones into a refined R-Tree for following QGC launches.
     */
    QString resultsString = "";
    QList<int> viewportZoneIndexes; // TODO: fill this variable
    int results = _zoneTree.Search(min, max, [this, &viewportZoneIndexes, &resultsString](const int& index) {
        viewportZoneIndexes.push_back(index);
        // Debug
        resultsString += QString::number(index) + " (" + _zones[index].name + ", minLonLat: " + QString::number(_zones[index].minLonLat[0]) + "|" + QString::number(_zones[index].minLonLat[1]) + ", maxLonLat: " + QString::number(_zones[index].maxLonLat[0]) + "|" + QString::number(_zones[index].maxLonLat[1]) + "), ";
        //qDebug() << "Found zone in viewport: idx " << index << ", Name: " << _zones[index].name << "), minAlt: " << _zones[index].minAltitude << ", polygon points: " << _zones[index].polygon.size();
        return true; // continue searching
    });
    qDebug() << "Zones in viewport " << results << ": " << resultsString << ")";

    // Go through each zone's nearby intersecting zones and clip them based on altitude layers
    for (int i : viewportZoneIndexes) {
        // Skip already clipped zones
        if (_zones[i].alreadyClipped) {
            qDebug() << "Skipping zone " << _zones[i].name << " (minAlt " << _zones[i].minAltitude << ") because it has already been clipped by higher priority zones";
            continue;
        }

        // Find indexes of intersecting zones
        QList<int> intersectingIndexes;
        int intersectingZoneNumber = _zoneTree.Search(_zones[i].minLonLat, _zones[i].maxLonLat, [this, &intersectingIndexes](const int& index) {
            intersectingIndexes.push_back(index);
            return true; // continue searching
        });
        qDebug() << "---- About to check intersection between zone " << _zones[i].name << " (minAlt " << _zones[i].minAltitude << ") and " << intersectingZoneNumber << " other zones in the viewport";

        Clipper2Lib::PathsD currentPath = { geoZoneToClipperPath(_zones[i].polygon) };
        for (int j : intersectingIndexes) {
            // Only clip current zone intersections with OTHER zones of equal or higher priority (lower minAltitude)
            if (i == j || _zones[i].minAltitude < _zones[j].minAltitude) {
                qDebug() << "Skipping zone " << _zones[j].name << " (minAlt " << _zones[j].minAltitude << ")";
                continue;
            }
            qDebug() << "Checking intersection between zone " << _zones[i].name << " (minAlt " << _zones[i].minAltitude << ") and zone " << _zones[j].name << " (minAlt " << _zones[j].minAltitude << ")";

            /*if (_zones[i].id == "A632187") {
                qDebug() << "------- REACHED ZONE A632187 -------";
            }*/

            Clipper2Lib::PathsD intersectingPath = { geoZoneToClipperPath(_zones[j].polygon) };
            currentPath = Clipper2Lib::Difference(currentPath, intersectingPath, Clipper2Lib::FillRule::NonZero, GEOZONE_CLIPPER_PRECISION);

            if (currentPath.empty()) {
                _zones[i].polygon.clear();
                _zoneTree.Remove(_zones[i].minLonLat, _zones[i].maxLonLat, i);
                qDebug() << "Zone " << _zones[i].name << " completely covered by higher priority zones, removing from tree and skipping further clipping, path size: " << currentPath.size();
                break;
            } else {
                _zones[i].polygon = clipperPathToGeoZone(currentPath[0]);
                qDebug() << "Clipped zone " << _zones[i].name << " (" << _zones[i].minAltitude << "m)" << " with zone " << _zones[j].name << " (" << _zones[j].minAltitude << "m), remaining polygon points: " << _zones[i].polygon.size() << ", path size: " << currentPath.size();

                // Update bounding box after clipping
                //insertZoneIntoTree(_zones[i], i);

                // Split further subzones into new zones of same ID and minAlt
                if (currentPath.size() > 1) {
                    for (int k = 1; k < currentPath.size(); k++) {
                        GeoZone newSubZone;
                        newSubZone.id = _zones[i].id;
                        newSubZone.name = _zones[i].name;
                        newSubZone.minAltitude = _zones[i].minAltitude;
                        newSubZone.maxAltitude = _zones[i].maxAltitude;
                        newSubZone.polygon = clipperPathToGeoZone(currentPath[k]);
                        newSubZone.alreadyClipped = true;
                        _zones.append(newSubZone);
                        insertZoneIntoTree(_zones.last(), _zones.size() - 1);
                        qDebug() << "Zone " << _zones[i].name << " has been split into " << currentPath.size() << " polygons with minAltitude " << _zones[i].minAltitude << " after clipping with zone " << _zones[j].name;
                    }
                }
            }
        }

        _zones[i].alreadyClipped = true;
        qDebug() << "Set zone " << _zones[i].name << " (minAlt " << _zones[i].minAltitude << ") as clipped by higher priority zones, final polygon points: " << _zones[i].polygon.size();
        break; // TODO: remove this break to handle all zones in the viewport
    }

    // Update model with clipped zones in the viewport
    _model.setZones(_zones);

    // For demo purposes, just display one of the zones in the viewport (cycling through them on each update)
    /*if (++displayedZone >= _zones.size())
        displayedZone = 0;
    _model.setZones({_zones[displayedZone]});
    qDebug() << "Displaying zone Index: " << displayedZone << ", Name: " << _zones[displayedZone].name;*/
}

void GeoZoneManager::insertZoneIntoTree(GeoZone& zone, int index)
{
    // Compute bounding box for the zone
    double minLon = std::numeric_limits<double>::max();
    double minLat = std::numeric_limits<double>::max();
    double maxLon = std::numeric_limits<double>::lowest();
    double maxLat = std::numeric_limits<double>::lowest();

    for (const QGeoCoordinate& coord : zone.polygon) {
        minLon = std::min(minLon, coord.longitude());
        minLat = std::min(minLat, coord.latitude());
        maxLon = std::max(maxLon, coord.longitude());
        maxLat = std::max(maxLat, coord.latitude());
    }

    // Put min/max datas into arrays that can be passed by reference
    double min[2] = { minLon, minLat };
    double max[2] = { maxLon, maxLat };

    // Cache bounding box in the zone for future intersection tests
    zone.minLonLat[0] = minLon;
    zone.minLonLat[1] = minLat;
    zone.maxLonLat[0] = maxLon;
    zone.maxLonLat[1] = maxLat;

    // Insert into R-tree (using zone index as data)
    _zoneTree.Insert(min, max, index);
}
