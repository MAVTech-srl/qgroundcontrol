target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        clipper.core.h
        clipper.engine.cc
        clipper.engine.h
        clipper.export.h
        clipper.h
        clipper.minkowski.h
        clipper.offset.cc
        clipper.offset.h
        clipper.rectclip.cc
        clipper.rectclip.h
        clipper.triangulation.cc
        clipper.triangulation.h
        clipper.version.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

#===========================================================================#

CPMAddPackage(
    NAME Clipper2
    VERSION 2.0.1
    GITHUB_REPOSITORY AngusJohnson/Clipper2
    GIT_TAG Clipper2_2.0.1
    OPTIONS
        "BUILD_APPS OFF"
        "BUILD_TESTING OFF"
)

#target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE clipper2)
