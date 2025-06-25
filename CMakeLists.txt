cmake_minimum_required(VERSION 3.10)

# Read version from version.txt
file(STRINGS "${CMAKE_SOURCE_DIR}/version.txt" PROJECT_VERSION LIMIT_COUNT 1)
project(fcitx5-lekhika VERSION ${PROJECT_VERSION})


# Find required Fcitx5 packages
find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Utils REQUIRED)
find_package(Fcitx5Config REQUIRED)

# Include Fcitx5 compiler settings
include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

# Add include directories
include_directories(${FCITX5_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Create the module library
add_library(fcitx5-lekhika MODULE lekhika.cpp)
set_target_properties(
    fcitx5-lekhika
    PROPERTIES
        PREFIX ""           # No "lib" prefix
        SUFFIX ".so"        # Shared object suffix
        OUTPUT_NAME "fcitx5lekhika"  # Output name
        LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)

# Link against Fcitx5 libraries
target_link_libraries(
    fcitx5-lekhika
    PRIVATE
        Fcitx5::Core
        Fcitx5::Utils
        Fcitx5::Config
)

# Install the engine
install(
    TARGETS fcitx5-lekhika  # Library target name
    DESTINATION "${FCITX_INSTALL_LIBDIR}/fcitx5"
)

# Install addon configuration (shared object metadata)
install(
    FILES config/fcitx5lekhika.addon.conf
    DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon"
    RENAME fcitx5lekhika.conf
)

# Install input method descriptor 
install(
    FILES config/fcitx5lekhika.inputmethod.intry.desc
    DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod"
    RENAME fcitx5lekhika.conf
)

# Install runtime data files
install(
    FILES
        data/mapping.toml
        data/autocorrect.toml
    DESTINATION "/usr/share/fcitx5/fcitx5-lekhika"
)

# Add icons subdirectory for additional resources
add_subdirectory(icons)
install(CODE "message(STATUS \"Icons installing\")")
