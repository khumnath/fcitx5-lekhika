cmake_minimum_required(VERSION 3.14)
project(fcitx5-lekhika LANGUAGES CXX)

file(STRINGS "${CMAKE_SOURCE_DIR}/version.txt" PROJECT_VERSION LIMIT_COUNT 1)
set(PROJECT_VERSION ${PROJECT_VERSION})
message(STATUS "Project version: ${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "Runtime")
include(GNUInstallDirs)

find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Utils REQUIRED)
find_package(Fcitx5Config REQUIRED)
find_package(SQLite3)
find_package(liblekhika)
find_package(ICU REQUIRED COMPONENTS uc i18n)

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")


# --------------------------------------------------------------------
# MODULE: fcitx5-lekhika
# --------------------------------------------------------------------
add_library(fcitx5-lekhika MODULE
    src/lekhika-addon.cpp
    src/lekhika-addon.h
)

target_include_directories(fcitx5-lekhika PRIVATE
    ${FCITX5_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(fcitx5-lekhika PRIVATE
    Fcitx5::Core
    Fcitx5::Utils
    Fcitx5::Config
    liblekhika::liblekhika
)

if(SQLite3_FOUND)
    message(STATUS "SQLite3 found: enabling dictionary features in module.")
    target_compile_definitions(fcitx5-lekhika PRIVATE HAVE_SQLITE3)
    target_link_libraries(fcitx5-lekhika PRIVATE SQLite::SQLite3)
endif()

set_target_properties(fcitx5-lekhika PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    OUTPUT_NAME "fcitx5lekhika"
    LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)


# --------------------------------------------------------------------
# Extra files and Install Targets
# --------------------------------------------------------------------
add_custom_target(lekhika-extra-files
    SOURCES
        fcitx5-lekhika.metainfo.xml
        config/fcitx5lekhika.conf
        config/fcitx5lekhika.addon.conf
        version.txt
        README.md
        LICENSE
)

install(TARGETS fcitx5-lekhika
    DESTINATION "${FCITX_INSTALL_LIBDIR}/fcitx5"
)

install(FILES config/fcitx5lekhika.addon.conf
    DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon"
    RENAME fcitx5lekhika.conf
)

install(FILES config/fcitx5lekhika.conf
    DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod"
)

install(FILES fcitx5-lekhika.metainfo.xml
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo"
)

add_subdirectory(icons)
install(CODE "message(STATUS \"Installing icons...\")")

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY
)

add_custom_target(uninstall-lekhika
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake"
)

