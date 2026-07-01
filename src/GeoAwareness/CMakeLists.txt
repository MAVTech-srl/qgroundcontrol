target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        GeoZoneManager.cc
        GeoZoneManager.h
        GeoZoneOverlay.cc
        GeoZoneOverlay.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
