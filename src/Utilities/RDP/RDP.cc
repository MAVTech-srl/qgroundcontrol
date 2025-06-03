/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "RDP.h"

QList<QGeoCoordinate> RDP::simplify(const QList<QGeoCoordinate>& points, double epsilon)
{
    if (points.size() < 3) return points;

    // Find the point with the maximum distance
    double maxDistance = 0.0;
    int index = 0;
    for (int i = 1; i < points.size() - 1; ++i) {
        double dist = perpendicularDistance(points[i], points.first(), points.last());
        if (dist > maxDistance) {
            index = i;
            maxDistance = dist;
        }
    }

    // If max distance is greater than epsilon, recursively simplify
    if (maxDistance > epsilon) {
        QList<QGeoCoordinate> recResults1 = simplify(points.mid(0, index + 1), epsilon);
        QList<QGeoCoordinate> recResults2 = simplify(points.mid(index), epsilon);

        // Combine results (avoid duplicating the index point)
        recResults1.removeLast();
        recResults1.append(recResults2);
        return recResults1;
    } else {
        // No point is farther than epsilon; return start and end
        return { points.first(), points.last() };
    }
}

double RDP::perpendicularDistance(const QGeoCoordinate& pt, const QGeoCoordinate& lineStart, const QGeoCoordinate& lineEnd)
{
    if (lineStart == lineEnd) {
        return pt.distanceTo(lineStart);
    }

    // Project point onto the line (using vector math)
    double x1 = lineStart.longitude();
    double y1 = lineStart.latitude();
    double x2 = lineEnd.longitude();
    double y2 = lineEnd.latitude();
    double x0 = pt.longitude();
    double y0 = pt.latitude();

    double num = std::abs((y2 - y1)*x0 - (x2 - x1)*y0 + x2*y1 - y2*x1);
    double den = std::sqrt((y2 - y1)*(y2 - y1) + (x2 - x1)*(x2 - x1));

    // Convert from degrees to meters using distanceTo (approximate)
    QGeoCoordinate proj = QGeoCoordinate(
        y1 + ((y2 - y1) * ((x0 - x1)*(x2 - x1) + (y0 - y1)*(y2 - y1))) / ((x2 - x1)*(x2 - x1) + (y2 - y1)*(y2 - y1)),
        x1 + ((x2 - x1) * ((x0 - x1)*(x2 - x1) + (y0 - y1)*(y2 - y1))) / ((x2 - x1)*(x2 - x1) + (y2 - y1)*(y2 - y1))
    );

    return pt.distanceTo(proj);
}
