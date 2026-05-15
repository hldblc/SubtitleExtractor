cmake_minimum_required(VERSION 3.16)
project(subtitle_extractor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Shared static library used by both CLI and GUI targets.
add_library(subext_lib STATIC
    src/EmbeddedExtractor.cpp
    src/WhisperExtractor.cpp
    src/OcrExtractor.cpp
    src/ExtractorFactory.cpp
    src/SubtitleWriter.cpp
    src/ProcessRunner.cpp
    src/Logger.cpp
)
target_include_directories(subext_lib PUBLIC include)

# CLI executable
add_executable(subtitle_extractor src/main.cpp)
target_link_libraries(subtitle_extractor PRIVATE subext_lib)

# IMPORTANT: keep in sync with installer.iss's MyAppVersion. A polished
# setup would generate both from one source via configure_file(), but
# manual sync is fine for a learning project — just bump in two places.
set(APP_VERSION "1.0.3")

# Qt GUI executable (only built when Qt is found).
# Network is needed by UpdateChecker for the GitHub Releases API call.
find_package(Qt6 COMPONENTS Widgets Concurrent Network QUIET)

if(Qt6_FOUND)
    message(STATUS "Qt6 ${Qt6_VERSION} found - building GUI target")

    set(GUI_SOURCES
        src/gui_main.cpp
        src/MainWindow.cpp
        src/UpdateChecker.cpp
        include/MainWindow.h
        include/UpdateChecker.h
    )

    # Conditionally pull in the icon if the user has dropped one at
    # resources/icon.ico. Without the file, the build still works —
    # Qt just falls back to its default icon.
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/icon.ico")
        message(STATUS "Icon found - bundling resources/icon.ico")
        list(APPEND GUI_SOURCES resources/app.qrc)
        if(WIN32)
            list(APPEND GUI_SOURCES resources/app.rc)
        endif()
    else()
        message(STATUS "No resources/icon.ico - GUI will use default Qt icon")
    endif()

    add_executable(subtitle_extractor_gui WIN32 ${GUI_SOURCES})

    target_link_libraries(subtitle_extractor_gui PRIVATE
        subext_lib
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Network
    )

    target_compile_definitions(subtitle_extractor_gui PRIVATE
        APP_VERSION="${APP_VERSION}"
    )

    if(WIN32)
        get_target_property(_qt_qmake_exe Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin_dir "${_qt_qmake_exe}" DIRECTORY)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
        if(WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET subtitle_extractor_gui POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                        --no-translations
                        --no-system-d3d-compiler
                        --no-opengl-sw
                        "$<TARGET_FILE:subtitle_extractor_gui>"
                COMMENT "Running windeployqt to bundle Qt DLLs"
            )
        endif()
    endif()
else()
    message(STATUS "Qt6 NOT found - GUI target skipped.")
    message(STATUS "Configure with -DCMAKE_PREFIX_PATH=C:/Qt/6.X.Y/msvc2022_64 to enable.")
endif()
