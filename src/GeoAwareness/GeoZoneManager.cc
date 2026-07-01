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

Clipper2Lib::PathsD GeoZoneManager::geoZoneToClipperPaths(const QList<QGeoCoordinate>& polygon, const QList<QGeoCoordinate>& hole)
{
    Clipper2Lib::PathsD result;

    Clipper2Lib::PathD outerPath;
    outerPath.reserve(polygon.size());
    for (const QGeoCoordinate& coord : polygon) {
        outerPath.emplace_back(coord.longitude(), coord.latitude());
    }

    if (outerPath.size() >= 3) {
        result.push_back(outerPath);
    }

    Clipper2Lib::PathD holePath;
    holePath.reserve(hole.size());
    for (const QGeoCoordinate& coord : hole) {
        holePath.emplace_back(coord.longitude(), coord.latitude());
    }

    if (holePath.size() >= 3) {
        if (!result.empty()) {
            // Ensure hole has opposite winding from outer for NonZero fill rule.
            if ((Clipper2Lib::Area<double>(result[0]) > 0) == (Clipper2Lib::Area<double>(holePath) > 0)) {
                std::reverse(holePath.begin(), holePath.end());
            }
        }
        result.push_back(holePath);
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
    /*_lastViewportZoneIndexes.clear();
    _hasViewportZoneCache = false;*/

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
        Clipper2Lib::PathsD zone_paths = geoZoneToClipperPaths(zone.polygon, zone.hole);
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
        resultsString += QString::number(index) + " (" + _zones[index].name + /*", minLonLat: " + QString::number(_zones[index].minLonLat[0]) + "|" + QString::number(_zones[index].minLonLat[1]) + ", maxLonLat: " + QString::number(_zones[index].maxLonLat[0]) + "|" + QString::number(_zones[index].maxLonLat[1]) +*/ "), ";
        //qDebug() << "Found zone in viewport: idx " << index << ", Name: " << _zones[index].name << "), minAlt: " << _zones[index].minAltitude << ", polygon points: " << _zones[index].polygon.size();
        return true; // continue searching
    });
    qDebug() << "Zones in viewport " << results << ": " << resultsString << ")";

    /*std::sort(viewportZoneIndexes.begin(), viewportZoneIndexes.end());

    // Cache hit: visible zone set unchanged, so clipped output is unchanged too.
    if (_hasViewportZoneCache && (viewportZoneIndexes == _lastViewportZoneIndexes)) {
        return;
    }

    if (viewportZoneIndexes.isEmpty()) {
        _lastViewportZoneIndexes = viewportZoneIndexes;
        _hasViewportZoneCache = true;
        _model.setZones({});
        return;
    }

    QList<GeoZone> clippedViewportZones;
    clippedViewportZones.reserve(viewportZoneIndexes.size());*/

    // Go through each zone's nearby intersecting zones and clip them based on altitude layers
    for (int i : viewportZoneIndexes) {
        // Skip already clipped zones
        if (_zones[i].alreadyClipped) {
            qDebug() << "Skipping zone " << _zones[i].name << " (minAlt " << _zones[i].minAltitude << ") because it has already been clipped by higher priority zones";
            continue;
        }

        /*if (_zones[i].polygon.size() < 3) {
            continue;
        }

        Clipper2Lib::PathsD clippedPaths = geoZoneToClipperPaths(_zones[i].polygon, _zones[i].hole);
        bool hasOverlapClipping = false;*/

        // Find indexes of intersecting zones
        QList<int> intersectingIndexes;
        int intersectingZoneNumber = _zoneTree.Search(_zones[i].minLonLat, _zones[i].maxLonLat, [this, &intersectingIndexes](const int& index) {
            intersectingIndexes.push_back(index);
            return true; // continue searching
        });
        qDebug() << "-------- About to check intersection between zone " << _zones[i].name << " (minAlt " << _zones[i].minAltitude << ") and " << intersectingZoneNumber << " other zones in the viewport";

        Clipper2Lib::PathsD currentPath = geoZoneToClipperPaths(_zones[i].polygon, _zones[i].hole);
        if (currentPath.empty()) {
            continue;
        }

        Clipper2Lib::ClipperD clipper(GEOZONE_CLIPPER_PRECISION);
        clipper.AddSubject(currentPath);
        bool hasClipPaths = false;
        for (int j : intersectingIndexes) {
            // Only clip current zone intersections with OTHER zones of equal or higher priority (lower minAltitude)
            if (i == j || _zones[i].minAltitude < _zones[j].minAltitude) {
                //qDebug() << "Skipping zone " << _zones[j].name << " (minAlt " << _zones[j].minAltitude << ")";
                continue;
            }

            Clipper2Lib::PathsD intersectingPath = geoZoneToClipperPaths(_zones[j].polygon, _zones[j].hole);
            if (intersectingPath.empty()) {
                continue;
            }

            qDebug() << "Checking intersection between zone " << _zones[i].name << " (minAlt " << _zones[i].minAltitude << "m) and zone " << _zones[j].name << " (minAlt " << _zones[j].minAltitude << "m)";
            clipper.AddClip(intersectingPath);
            hasClipPaths = true;
            //currentPath = Clipper2Lib::Difference(currentPath, {intersectingPath}, Clipper2Lib::FillRule::NonZero, GEOZONE_CLIPPER_PRECISION);

            //qDebug() << "After clipping with zone " << _zones[j].name << " (minAlt " << _zones[j].minAltitude << "), subject size: " << currentPath.size();
        }

        // If no valid clip zone was added, clipping must be skipped to preserve the subject geometry.
        if (!hasClipPaths) {
            _zones[i].alreadyClipped = true;
            continue;
        }

        // Clip the subject (zone[i]) against all intersecting zones (zone[j]) at once, storing the result in a PolyTreeD structure
        Clipper2Lib::PolyTreeD polyTree;
        clipper.Execute(Clipper2Lib::ClipType::Difference, Clipper2Lib::FillRule::NonZero, polyTree);

        const double oldMin[2] = { _zones[i].minLonLat[0], _zones[i].minLonLat[1] };
        const double oldMax[2] = { _zones[i].maxLonLat[0], _zones[i].maxLonLat[1] };

        // Split top-level clipped contours into separate GeoZones (same metadata as subject zone).
        QList<GeoZone> splitZones;
        for (auto outerIterator = polyTree.begin(); outerIterator != polyTree.end(); outerIterator++) {
            const Clipper2Lib::PolyPathD* outer = outerIterator->get();
            if (outer->IsHole() || outer->Polygon().size() < 3) {
                continue;
            }

            GeoZone splitZone = _zones[i];
            splitZone.polygon = clipperPathToGeoZone(outer->Polygon());
            splitZone.hole.clear();

            for (auto holeIterator = outer->begin(); holeIterator != outer->end(); holeIterator++) {
                const Clipper2Lib::PolyPathD* hole = holeIterator->get();
                if (!hole->IsHole() || hole->Polygon().size() < 3) {
                    continue;
                }

                // GeoZone currently stores a single hole ring. Keep the first valid hole.
                splitZone.hole = clipperPathToGeoZone(hole->Polygon());
                break;
            }

            splitZones.append(splitZone);
        }

        if (splitZones.isEmpty()) {
            _zoneTree.Remove(oldMin, oldMax, i);
            _zones[i].polygon.clear();
            _zones[i].hole.clear();
        } else {
            // Keep first split in original slot.
            _zoneTree.Remove(oldMin, oldMax, i);
            _zones[i].polygon = splitZones.first().polygon;
            _zones[i].hole = splitZones.first().hole;
            insertZoneIntoTree(_zones[i], i);

            // Additional splits become new zones with same id/name/type/altitudes.
            for (int splitIndex = 1; splitIndex < splitZones.size(); ++splitIndex) {
                GeoZone newSplitZone = splitZones[splitIndex];
                newSplitZone.alreadyClipped = true;
                _zones.append(newSplitZone);
                insertZoneIntoTree(_zones.last(), _zones.size() - 1);
            }
        }

        qDebug() << "After PolyTreeD clipping zone " << _zones[i].name << " resulting split zones:" << splitZones.size();

        _zones[i].alreadyClipped = true;
        //qDebug() << "Set zone " << _zones[i].name << " (minAlt " << _zones[i].minAltitude << ") as clipped by higher priority zones, final polygon points: " << _zones[i].polygon.size();
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
