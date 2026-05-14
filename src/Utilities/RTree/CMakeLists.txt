target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        RTree.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

#===========================================================================#

CPMAddPackage(
    NAME RTree
    VERSION 1.0
    GITHUB_REPOSITORY nushoin/RTree
    OPTIONS
        "BUILD_APPS OFF"
        "BUILD_TESTING OFF"
)

#target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE RTree)
