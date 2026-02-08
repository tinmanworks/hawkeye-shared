cmake_minimum_required(VERSION 3.16)

project(hawkeye-shared
    VERSION 1.0.0
    LANGUAGES CXX
)

message(STATUS "[hawkeye-shared] Configuring hawkeye-shared v${PROJECT_VERSION}")

# -----------------------------------------------------------------------------
# hawkeye-shared-headers (header-only): protocol/include-only usage
# -----------------------------------------------------------------------------
add_library(hawkeye-shared-headers INTERFACE)

target_include_directories(hawkeye-shared-headers
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(hawkeye-shared-headers
    INTERFACE
        cxx_std_17
)

# -----------------------------------------------------------------------------
# hawkeye-shared (compiled): shared services (e.g. TransmissionService)
# -----------------------------------------------------------------------------
add_library(hawkeye-shared STATIC
    src/services/TransmissionService.cpp
)

target_include_directories(hawkeye-shared
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(hawkeye-shared PUBLIC cxx_std_17)

# Ensure anyone linking hawkeye-shared gets the headers too.
target_link_libraries(hawkeye-shared PUBLIC hawkeye-shared-headers)

# TransmissionService depends on FrameKit types.
# IMPORTANT: the end application must add_subdirectory(FrameKit) BEFORE hawkeye-shared
# so this target exists at configure time.
#
# Use the correct target name exported by FrameKit in your project:
# - If FrameKit exports FrameKit::FrameKit, use that
# - If it exports FrameKit, use that
if (TARGET FrameKit::FrameKit)
    target_link_libraries(hawkeye-shared PUBLIC FrameKit::FrameKit)
elseif (TARGET FrameKit)
    target_link_libraries(hawkeye-shared PUBLIC FrameKit)
else()
    message(FATAL_ERROR
        "[hawkeye-shared] FrameKit target not found. "
        "Make sure your top-level CMakeLists.txt does add_subdirectory(FrameKit) "
        "before add_subdirectory(hawkeye-shared). Expected target FrameKit::FrameKit or FrameKit."
    )
endif()
