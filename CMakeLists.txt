cmake_minimum_required(VERSION 3.20)

project(SerialTerminal
    VERSION 0.1.0
    DESCRIPTION "Lightweight Linux serial port terminal with Qt/KDE and GTK4/Libadwaita frontends"
    LANGUAGES CXX
)

include(GNUInstallDirs)

option(BUILD_QT "Build Qt6 / KDE frontend (serial-terminal)" ON)
option(BUILD_GNOME "Build GTK4 / Libadwaita frontend (serial-terminal-gnome)" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(APP_ID "com.okienko.SerialTerminal")
set(APP_ID_GNOME "com.okienko.SerialTerminal.Gnome")

set(QT_TARGET_NAME "serial-terminal")
set(GNOME_TARGET_NAME "serial-terminal-gnome")

if(NOT BUILD_QT AND NOT BUILD_GNOME)
    message(FATAL_ERROR "At least one of BUILD_QT or BUILD_GNOME must be ON.")
endif()

# Shared icon
install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/icons/hicolor/scalable/apps/${APP_ID}.svg"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
)

if(BUILD_QT)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    find_package(KF6WindowSystem REQUIRED)

    add_executable(${QT_TARGET_NAME} main.cpp)
    target_compile_definitions(${QT_TARGET_NAME} PRIVATE
        APP_VERSION="${PROJECT_VERSION}"
        APP_ID="${APP_ID}"
    )
    target_compile_options(${QT_TARGET_NAME} PRIVATE -Wall -Wextra)
    target_link_libraries(${QT_TARGET_NAME} PRIVATE
        Qt6::Widgets
        KF6::WindowSystem
    )

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/data/${APP_ID}.desktop.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${APP_ID}.desktop"
        @ONLY
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/data/${APP_ID}.metainfo.xml.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${APP_ID}.metainfo.xml"
        @ONLY
    )

    install(TARGETS ${QT_TARGET_NAME}
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${APP_ID}.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${APP_ID}.metainfo.xml"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo"
    )
endif()

if(BUILD_GNOME)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
    pkg_check_modules(LIBADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)

    add_executable(${GNOME_TARGET_NAME} main_gnome.cpp)
    target_compile_definitions(${GNOME_TARGET_NAME} PRIVATE
        APP_VERSION="${PROJECT_VERSION}"
        APP_ID="${APP_ID_GNOME}"
    )
    target_compile_options(${GNOME_TARGET_NAME} PRIVATE -Wall -Wextra)
    target_link_libraries(${GNOME_TARGET_NAME} PRIVATE
        PkgConfig::GTK4
        PkgConfig::LIBADWAITA
    )

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/data/${APP_ID_GNOME}.desktop.in"
        "${CMAKE_CURRENT_BINARY_DIR}/${APP_ID_GNOME}.desktop"
        @ONLY
    )

    install(TARGETS ${GNOME_TARGET_NAME}
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${APP_ID_GNOME}.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    )
endif()
