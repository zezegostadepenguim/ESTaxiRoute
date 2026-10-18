cmake_minimum_required(VERSION 3.15)
project(ESTaxiRoute CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core logic (no EuroScope dependency) + unit tests, buildable on any platform.
add_library(taxicore STATIC src/TaxiCore.cpp)
target_include_directories(taxicore PUBLIC src)

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core taxicore)
add_test(NAME test_core COMMAND test_core)

# The plugin DLL. Copy EuroScopePlugIn.h and EuroScopePlugInDll.lib from the
# "PlugInEnvironment" folder of your EuroScope install into sdk/.
if(WIN32)
    set(ES_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk" CACHE PATH "Folder with EuroScopePlugIn.h and EuroScopePlugInDll.lib")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET taxicore test_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    add_library(ESTaxiRoute SHARED
        src/main.cpp
        src/TaxiRoutePlugin.cpp
        src/TaxiRouteScreen.cpp)
    target_include_directories(ESTaxiRoute PRIVATE ${ES_SDK_DIR})
    target_compile_definitions(ESTaxiRoute PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(ESTaxiRoute PRIVATE taxicore "${ES_SDK_DIR}/EuroScopePlugInDll.lib")
endif()
