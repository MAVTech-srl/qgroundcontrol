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
#include <QtConcurrent>
#include <QElapsedTimer>
#include <numeric>
#include <vector>
#include <algorithm>
#include <QtQml/qqml.h>

#define GEOZONE_CLIPPER_PRECISION 8

Q_APPLICATION_STATIC(GeoZoneManager, _geoZoneManager);

GeoZoneManager::GeoZoneManager(QObject* parent): QObject(parent)
{
    connect(&_loadWatcher, &QFutureWatcher<QList<GeoZone>>::finished, this, &GeoZoneManager::_onLoadFinished);
}

GeoZoneManager::~GeoZoneManager()
{
    if (_cancelLoad) {
        _cancelLoad->store(true);
    }
    _loadWatcher.waitForFinished();
}

void GeoZoneManager::init()
{
    Fact* airspaceFileFact = SettingsManager::instance()->flightMapSettings()->airspaceFilePath();
    QString path = airspaceFileFact->rawValue().toString();
    loadFromFile(path);
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

Clipper2Lib::PathsD GeoZoneManager::geoZoneToClipperPaths(const QList<QGeoCoordinate>& polygon, const QList<QList<QGeoCoordinate>>& holes)
{
    Clipper2Lib::PathsD result;

    Clipper2Lib::PathD outerPath;
    outerPath.reserve(polygon.size());
    for (const QGeoCoordinate& coord : polygon) {
        outerPath.emplace_back(coord.longitude(), coord.latitude());
    }

    if (outerPath.size() >= 3) {
        // Normalize all outer rings to the same winding to prevent NonZero cancellation across independent overlapping zones.
        if (Clipper2Lib::Area<double>(outerPath) <= 0) {
            std::reverse(outerPath.begin(), outerPath.end());
        }
        result.push_back(outerPath);
    }
    
    for (const QList<QGeoCoordinate>& hole : holes) {
        Clipper2Lib::PathD holePath;
        holePath.reserve(hole.size());
        for (const QGeoCoordinate& coord : hole) {
            holePath.emplace_back(coord.longitude(), coord.latitude());
        }

        if (holePath.size() < 3 || result.empty()) {
            continue;
        }

        // Ensure each hole has opposite winding from outer for NonZero fill rule.
        if ((Clipper2Lib::Area<double>(result[0]) > 0) == (Clipper2Lib::Area<double>(holePath) > 0)) {
            std::reverse(holePath.begin(), holePath.end());
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

void GeoZoneManager::_setLoading(bool loading)
{
    if (_loading != loading) {
        _loading = loading;
        emit loadingChanged();
    }
}

void GeoZoneManager::loadFromFile(const QString& path)
{
    // Abandon any load still in flight; its result is never applied
    if (_cancelLoad) {
        _cancelLoad->store(true);
    }
    _cancelLoad.reset();
    _loadWatcher.setFuture(QFuture<QList<GeoZone>>());

    _zones = {};
    _model.setZones({});
    emit countChanged();

    if (path.isEmpty()) {
        qDebug() << "Cleared GeoZones";
        _setLoading(false);
        return;
    }

    const CancelFlag cancel = std::make_shared<std::atomic_bool>(false);
    _cancelLoad = cancel;
    _setLoading(true);

    _loadWatcher.setFuture(QtConcurrent::run([path, cancel]() {
        QElapsedTimer timer;
        timer.start();
        const QList<GeoZone> parsed = parseFile(path, cancel);
        qDebug() << "Parsed" << parsed.size() << "GeoZones in " << timer.restart() << "ms";
        const QList<GeoZone> clipped = clipZones(parsed, cancel);
        qDebug() << "Clipped to" << clipped.size() << "GeoZones in " << timer.elapsed() << "ms";
        return clipped;
    }));
}

void GeoZoneManager::_onLoadFinished()
{
    if (!_cancelLoad || _cancelLoad->load()) {
        return;
    }

    _zones = _loadWatcher.result();
    _model.setZones(_zones);
    _cancelLoad.reset();
    emit countChanged();
    _setLoading(false);
}

QList<GeoZoneManager::GeoZone> GeoZoneManager::parseFile(const QString& path, const CancelFlag& cancel)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open GeoZone file: " << path;
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonArray features = doc.object()["features"].toArray();
    qDebug() << "Loaded " << features.size() << " features from file";

    QList<int> featureIndexes(features.size());
    std::iota(featureIndexes.begin(), featureIndexes.end(), 0);

    // Features are independent and the parsed document is read-only, so they can be converted in parallel
    const auto parseFeature = [&features, &cancel](int featureIndex) {
        QList<GeoZone> zones;
        if (cancel->load()) {
            return zones;
        }

        const QJsonObject fObj = features[featureIndex].toObject();
        const QString id = fObj["identifier"].toString();
        const QString name = fObj["name"].toString();

        // Some zones are split over multiple geometries (separated by higher priority zones), we need to add each geometry as its own zone and type
        const QJsonArray geometryArray = fObj["geometry"].toArray();
        for (const auto& geometry : geometryArray) {
            const QJsonObject zoneGeometry = geometry.toObject();
            GeoZone zone;
            zone.id = id;
            zone.name = name;
            zone.minAltitude = zoneGeometry["lowerLimit"].toDouble();
            zone.maxAltitude = zoneGeometry["upperLimit"].toDouble();

            const QJsonArray coordinateLists = zoneGeometry["horizontalProjection"].toObject()["coordinates"].toArray();
            if (coordinateLists.isEmpty()) {
                continue;
            }

            // Handle first polygon of the geometry object as the Polygon, the second (if present) as the Hole
            for (int i = 0; i < coordinateLists.size(); i++) {
                const QJsonArray coordList = coordinateLists[i].toArray();
                QList<QGeoCoordinate> geoCoordinates;
                geoCoordinates.reserve(coordList.size());
                for (const auto& coord : coordList) {
                    const QJsonArray point = coord.toArray();
                    if (point.size() < 2) {
                        continue;
                    }
                    geoCoordinates.append(QGeoCoordinate(point[1].toDouble(), point[0].toDouble()));
                }

                if (i == 0) {
                    zone.polygon = geoCoordinates;
                } else {
                    zone.holes.append(geoCoordinates);
                }
            }

            // Skip invalid polygons
            if (zone.polygon.size() < 3) {
                continue;
            }

            updateBoundingBox(zone);
            zones.append(zone);
        }

        return zones;
    };

    const QList<QList<GeoZone>> perFeature = QtConcurrent::blockingMapped(featureIndexes, parseFeature);

    QList<GeoZone> zones;
    for (const QList<GeoZone>& featureZones : perFeature) {
        zones.append(featureZones);
    }
    return zones;
}

QList<GeoZoneManager::GeoZone> GeoZoneManager::clipZones(const QList<GeoZone>& zones, const CancelFlag& cancel)
{
    if (zones.isEmpty()) {
        return {};
    }

    // Clipping only ever shrinks a zone, so zones interact only if their original bounding boxes overlap.
    // Group them into connected clusters which can be clipped independently (and in parallel).
    RTree<int, double, 2> tree;
    for (int i = 0; i < zones.size(); i++) {
        tree.Insert(zones[i].minLonLat, zones[i].maxLonLat, i);
    }

    std::vector<int> parent(zones.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto findRoot = [&parent](int i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    };

    for (int i = 0; i < zones.size(); i++) {
        tree.Search(zones[i].minLonLat, zones[i].maxLonLat, [&](const int& j) {
            const int rootI = findRoot(i);
            const int rootJ = findRoot(j);
            if (rootI != rootJ) {
                parent[std::max(rootI, rootJ)] = std::min(rootI, rootJ);
            }
            return true;
        });
    }

    // Zones keep their relative order within a cluster, which preserves the sequential clipping semantics
    QHash<int, int> clusterOfRoot;
    QList<QList<GeoZone>> clusters;
    for (int i = 0; i < zones.size(); i++) {
        const int root = findRoot(i);
        auto it = clusterOfRoot.find(root);
        if (it == clusterOfRoot.end()) {
            it = clusterOfRoot.insert(root, clusters.size());
            clusters.append(QList<GeoZone>());
        }
        clusters[it.value()].append(zones[i]);
    }

    // Largest clusters first so the pool isn't left waiting on one big job at the end
    std::sort(clusters.begin(), clusters.end(), [](const QList<GeoZone>& a, const QList<GeoZone>& b) {
        return a.size() > b.size();
    });

    const QList<QList<GeoZone>> clipped = QtConcurrent::blockingMapped(clusters, [&cancel](const QList<GeoZone>& cluster) {
        return clipCluster(cluster, cancel);
    });

    QList<GeoZone> result;
    result.reserve(zones.size());
    for (const QList<GeoZone>& cluster : clipped) {
        result.append(cluster);
    }
    return result;
}

QList<GeoZoneManager::GeoZone> GeoZoneManager::clipCluster(const QList<GeoZone>& clusterZones, const CancelFlag& cancel)
{
    if (clusterZones.size() < 2) {
        return clusterZones;
    }

    QList<GeoZone> zones = clusterZones;
    RTree<int, double, 2> tree;
    // Clipper paths are cached per zone since each zone is used as clip path by many others
    std::vector<Clipper2Lib::PathsD> clipperPaths;
    clipperPaths.reserve(zones.size());
    for (int i = 0; i < zones.size(); i++) {
        tree.Insert(zones[i].minLonLat, zones[i].maxLonLat, i);
        clipperPaths.push_back(geoZoneToClipperPaths(zones[i].polygon, zones[i].holes));
    }

    // Clip each zone against intersecting zones of equal or higher priority (lower minAltitude)
    for (int i = 0; i < zones.size(); i++) {
        if (cancel->load()) {
            return {};
        }

        if (zones[i].alreadyClipped) {
            continue;
        }

        // Find indexes of intersecting zones
        QList<int> intersectingIndexes;
        tree.Search(zones[i].minLonLat, zones[i].maxLonLat, [&intersectingIndexes](const int& index) {
            intersectingIndexes.push_back(index);
            return true; // continue searching
        });

        if (clipperPaths[i].empty()) {
            continue;
        }

        Clipper2Lib::ClipperD clipper(GEOZONE_CLIPPER_PRECISION);
        clipper.AddSubject(clipperPaths[i]);
        bool hasClipPaths = false;
        for (int j : intersectingIndexes) {
            if (i == j || zones[i].minAltitude < zones[j].minAltitude || clipperPaths[j].empty()) {
                continue;
            }

            clipper.AddClip(clipperPaths[j]);
            hasClipPaths = true;
        }

        // Without clip paths the subject geometry must be preserved as is
        if (!hasClipPaths) {
            zones[i].alreadyClipped = true;
            continue;
        }

        Clipper2Lib::PolyTreeD polyTree;
        clipper.Execute(Clipper2Lib::ClipType::Difference, Clipper2Lib::FillRule::NonZero, polyTree);

        const double oldMin[2] = { zones[i].minLonLat[0], zones[i].minLonLat[1] };
        const double oldMax[2] = { zones[i].maxLonLat[0], zones[i].maxLonLat[1] };

        // Split top-level clipped contours into separate zones (same metadata as the subject zone)
        QList<GeoZone> splitZones;
        for (auto outerIterator = polyTree.begin(); outerIterator != polyTree.end(); outerIterator++) {
            const Clipper2Lib::PolyPathD* outer = outerIterator->get();
            if (outer->IsHole() || outer->Polygon().size() < 3) {
                continue;
            }

            GeoZone splitZone = zones[i];
            splitZone.polygon = clipperPathToGeoZone(outer->Polygon());
            splitZone.holes.clear();

            for (auto holeIterator = outer->begin(); holeIterator != outer->end(); holeIterator++) {
                const Clipper2Lib::PolyPathD* hole = holeIterator->get();
                if (!hole->IsHole() || hole->Polygon().size() < 3) {
                    continue;
                }

                splitZone.holes.append(clipperPathToGeoZone(hole->Polygon()));
            }

            splitZones.append(splitZone);
        }

        tree.Remove(oldMin, oldMax, i);
        if (splitZones.isEmpty()) {
            // Completely covered by higher priority zones
            zones[i].polygon.clear();
            zones[i].holes.clear();
            clipperPaths[i].clear();
        } else {
            // First split stays in the original slot
            zones[i].polygon = splitZones.first().polygon;
            zones[i].holes = splitZones.first().holes;
            updateBoundingBox(zones[i]);
            tree.Insert(zones[i].minLonLat, zones[i].maxLonLat, i);
            clipperPaths[i] = geoZoneToClipperPaths(zones[i].polygon, zones[i].holes);

            // Additional splits become new zones
            for (int splitIndex = 1; splitIndex < splitZones.size(); splitIndex++) {
                GeoZone newSplitZone = splitZones[splitIndex];
                newSplitZone.alreadyClipped = true;
                updateBoundingBox(newSplitZone);
                zones.append(newSplitZone);
                clipperPaths.push_back(geoZoneToClipperPaths(newSplitZone.polygon, newSplitZone.holes));
                tree.Insert(newSplitZone.minLonLat, newSplitZone.maxLonLat, static_cast<int>(zones.size()) - 1);
            }
        }

        zones[i].alreadyClipped = true;
    }

    QList<GeoZone> result;
    result.reserve(zones.size());
    for (const GeoZone& zone : std::as_const(zones)) {
        if (zone.polygon.size() >= 3) {
            result.append(zone);
        }
    }
    return result;
}

void GeoZoneManager::updateBoundingBox(GeoZone& zone)
{
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

    zone.minLonLat[0] = minLon;
    zone.minLonLat[1] = minLat;
    zone.maxLonLat[0] = maxLon;
    zone.maxLonLat[1] = maxLat;
}
