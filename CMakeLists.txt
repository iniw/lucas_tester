cmake_minimum_required(VERSION 3.5)

project(LucasTester VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Bluetooth)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Bluetooth)

set(PROJECT_SOURCES
    main.cpp
    qtester.cpp
    qtester.h
    qtester.ui
)

qt_add_executable(LucasTester
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

set_property(TARGET LucasTester PROPERTY QT_ANDROID_TARGET_SDK_VERSION 33)
set_property(TARGET LucasTester PROPERTY QT_ANDROID_MIN_SDK_VERSION 29)

target_link_libraries(LucasTester PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Bluetooth
)

set_target_properties(LucasTester PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER lucas.tester.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

install(TARGETS LucasTester
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

qt_finalize_executable(LucasTester)
