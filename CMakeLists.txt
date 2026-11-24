cmake_minimum_required(VERSION 3.21)
project(kesBuildLauncher VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

add_library(kbl_core STATIC
    src/BuildProfile.h
    src/BuildProfile.cpp
    src/BuildPlan.h
    src/BuildPlan.cpp
    src/BuildRunner.h
    src/BuildRunner.cpp
    src/ProfileStore.h
    src/ProfileStore.cpp
    src/Toolchain.h
    src/Toolchain.cpp
)
target_include_directories(kbl_core PUBLIC src)
target_link_libraries(kbl_core PUBLIC Qt6::Core)

qt_add_executable(kesBuildLauncher
    src/main.cpp
    src/KesStyle.h
    src/KesStyle.cpp
    src/ProfileEditor.h
    src/ProfileEditor.cpp
    src/MainWindow.h
    src/MainWindow.cpp
)
target_link_libraries(kesBuildLauncher PRIVATE kbl_core Qt6::Widgets)

qt_add_resources(kesBuildLauncher "app_icon"
    PREFIX "/icons"
    BASE resource
    FILES resource/icon.png
)

if(WIN32)
    target_sources(kesBuildLauncher PRIVATE resource/kesBuildLauncher.rc)
endif()
set_target_properties(kesBuildLauncher PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

qt_add_executable(kesbuild src/cli_main.cpp)
target_link_libraries(kesbuild PRIVATE kbl_core)

foreach(target IN ITEMS kbl_core kesBuildLauncher kesbuild)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()

install(TARGETS kesBuildLauncher kesbuild
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

include(CTest)
if(BUILD_TESTING)
    add_executable(build_launcher_test tests/launcher_test.cpp)
    target_link_libraries(build_launcher_test PRIVATE kbl_core)
    add_test(NAME build_launcher_test COMMAND build_launcher_test)
    if(WIN32)
        set_tests_properties(build_launcher_test PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${Qt6_DIR}/../../../bin")
    endif()
endif()
