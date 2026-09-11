target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        GeoZoneManager.cc
        GeoZoneManager.h
        GeoZoneOverlay.cc
        GeoZoneOverlay.h
)

target_link_libraries(${CMAKE_PROJECT_NAME}
    PRIVATE
        earcut_hpp::earcut_hpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
