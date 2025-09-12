cmake_minimum_required(VERSION 3.10)
project(fcitx5-lekhika LANGUAGES CXX)

# Read version from version.txt
file(STRINGS "${CMAKE_SOURCE_DIR}/version.txt" PROJECT_VERSION LIMIT_COUNT 1)
set(PROJECT_VERSION ${PROJECT_VERSION})
message(STATUS "Project version: ${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "Runtime")
include(GNUInstallDirs)

# ─────────────────────────────────────────────────────────────
# Dependencies
# ─────────────────────────────────────────────────────────────
find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Utils REQUIRED)
find_package(Fcitx5Config REQUIRED)
find_package(SQLite3)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets  Network)
find_package(ICU REQUIRED COMPONENTS uc)

# Fcitx5 compiler settings
include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

# ─────────────────────────────────────────────────────────────
# Shared Sources
# ─────────────────────────────────────────────────────────────
set(LEKHIKA_CORE_SOURCES
    src/lekhika_core.cpp
    src/lekhika_core.h
)

# ─────────────────────────────────────────────────────────────
# MODULE: fcitx5-lekhika
# ─────────────────────────────────────────────────────────────
add_library(fcitx5-lekhika MODULE
    src/lekhika-addon.cpp
    src/lekhika-addon.h
    ${LEKHIKA_CORE_SOURCES}
)

target_include_directories(fcitx5-lekhika PRIVATE
    ${FCITX5_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(fcitx5-lekhika PRIVATE
    Fcitx5::Core
    Fcitx5::Utils
    Fcitx5::Config
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

# ─────────────────────────────────────────────────────────────
# GUI TOOL: lekhika-trainer
# ─────────────────────────────────────────────────────────────
add_executable(lekhika-trainer
    src/lekhika-trainer.cpp
    ${LEKHIKA_CORE_SOURCES}
)

target_include_directories(lekhika-trainer PRIVATE
    ${FCITX5_INCLUDE_DIRS} # For utils like StandardPath
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(lekhika-trainer PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    ICU::uc
    Fcitx5::Utils # For fcitx StandardPath
)

if(SQLite3_FOUND)
    target_compile_definitions(lekhika-trainer PRIVATE HAVE_SQLITE3)
    target_link_libraries(lekhika-trainer PRIVATE SQLite::SQLite3)
endif()


# ─────────────────────────────────────────────────────────────
# Extra files and Install Targets
# ─────────────────────────────────────────────────────────────
add_custom_target(lekhika-extra-files
    SOURCES
        data/mapping.toml
        data/autocorrect.toml
        data/lekhika-trainer.desktop
        lekhika-trainer.metainfo.xml
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

install(TARGETS lekhika-trainer
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(FILES config/fcitx5lekhika.addon.conf
    DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon"
    RENAME fcitx5lekhika.conf
)

install(FILES config/fcitx5lekhika.conf
    DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod"
)

install(FILES
    lekhika-trainer.metainfo.xml
    fcitx5-lekhika.metainfo.xml
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo"
)

install(FILES
    data/mapping.toml
    data/autocorrect.toml
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/fcitx5/fcitx5-lekhika"
)

install(FILES data/lekhika-trainer.desktop
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
)

add_subdirectory(icons)
install(CODE "message(STATUS \"Installing icons...\")")

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY
)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake"
)

