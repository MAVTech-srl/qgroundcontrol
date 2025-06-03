/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QList>
#include <QtPositioning/QGeoCoordinate>

class RDP
{
public:
    static QList<QGeoCoordinate> simplify(const QList<QGeoCoordinate>& points, double epsilon);

private:
    static double perpendicularDistance(const QGeoCoordinate& pt,
                                        const QGeoCoordinate& lineStart,
                                        const QGeoCoordinate& lineEnd);
};
