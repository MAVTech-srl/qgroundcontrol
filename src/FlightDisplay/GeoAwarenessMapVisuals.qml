/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.ScreenTools
import QGroundControl.Palette
import QGroundControl.Controls
import QGroundControl.FlightMap

/// GeoAwareness map visuals
Item {
    id: _root
    z: QGroundControl.zOrderMapItems

    property var    map
    property var    myGeoAwarenessController
    property bool   interactive:            false   ///< true: user can interact with items
    property bool   planView:               true    ///< true: visuals showing in plan view
    property var    homePosition

    property var    _polygons:                  myGeoFenceController.polygons
    property var    _circles:                   myGeoFenceController.circles
    property color  _borderColor:               "orange"
    property int    _borderWidthInclusion:      2
    property int    _borderWidthExclusion:      0
    property color  _interiorColorExclusion:    "orange"
    property color  _interiorColorInclusion:    "transparent"
    property real   _interiorOpacityExclusion:  0.2 * opacity
    property real   _interiorOpacityInclusion:  1 * opacity

    // By default the parent for Instantiator.delegate item is the Instatiator itself. By there is a bug
    // in Qt which will cause a crash if this delete item has Menu item within it. Since the Menu item
    // doesn't like having a non-visual item as parent. This is likely related to hybrid QQuickWidtget+QML
    // Hence Qt folks are going to care. In order to workaround you have to parent the item to _root Item instead.
    Instantiator {
        model: _polygons

        delegate : QGCMapPolygonVisuals {
            parent:             _root
            mapControl:         map
            mapPolygon:         object
            borderWidth:        _borderWidthExclusion
            borderColor:        _borderColor
            interiorColor:      _interiorColorExclusion
            interiorOpacity:    _interiorOpacityExclusion
            interactive:        _root.interactive && mapPolygon && mapPolygon.interactive
        }
    }

    Instantiator {
        model: _circles

        delegate : QGCMapCircleVisuals {
            parent:             _root
            mapControl:         map
            mapCircle:          object
            borderWidth:        _borderWidthExclusion
            borderColor:        _borderColor
            interiorColor:      _interiorColorExclusion
            interiorOpacity:    _interiorOpacityExclusion
            interactive:        _root.interactive && mapCircle && mapCircle.interactive
        }
    }

    // Circular geofence specified from parameter
    Component {
        id: paramCircleFenceComponent

        MapCircle {
            color:          _interiorColorInclusion
            opacity:        _interiorOpacityInclusion
            border.color:   _borderColor
            border.width:   _borderWidthInclusion
            center:         homePosition
            radius:         _radius
            visible:        homePosition.isValid && _radius > 0

            property real _radius: myGeoFenceController.paramCircularFence

            on_RadiusChanged: console.log("_radius", _radius, homePosition.isValid, homePosition)
        }
    }
}
