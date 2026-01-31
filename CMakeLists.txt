cmake_minimum_required(VERSION 3.16)


project(hawkeye-shared
    VERSION 1.0.0
    LANGUAGES CXX
)

message(STATUS "[hawkeye-shared] Configuring hawkeye-shared v${PROJECT_VERSION}")
 
# Header-only interface library
add_library(hawkeye-shared INTERFACE)

# Public include path
# Consumers will do: #include <hawkeye/protocol/...>
target_include_directories(hawkeye-shared
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Enforce modern C++
target_compile_features(hawkeye-shared
    INTERFACE
        cxx_std_17
)

# Optional: warnings if I ever add sources later
# target_compile_options(hawkeye-shared INTERFACE -Wall -Wextra -Wpedantic)
