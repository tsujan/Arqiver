cmake_minimum_required(VERSION 3.5.0)
project(arqiver)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_definitions(-Wall)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr")
endif()

set(QT6_MINIMUM_VERSION "6.6.0")

find_package(Qt6 "${QT6_MINIMUM_VERSION}" REQUIRED COMPONENTS Core Gui Widgets Svg DBus)

# optional localization
find_package(Qt6 QUIET COMPONENTS LinguistTools)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
                    ${Qt6Core_INCLUDE_DIRS}
                    ${Qt6Gui_INCLUDE_DIRS}
                    ${Qt6Widgets_INCLUDE_DIRS}
                    ${Qt6Svg_INCLUDE_DIRS}
                    ${Qt6DBus_INCLUDE_DIRS})

add_definitions(-DDATADIR="${CMAKE_INSTALL_PREFIX}/share")

set(arqiver_SRCS
    main.cpp
    backends.cpp
    config.cpp
    mainWin.cpp
    pref.cpp
    svgicons.cpp
    treeWidget.cpp)

set(arqiver_RESOURCES data/arq.qrc)
set(DESKTOP_FILE data/arqiver.desktop)
set(ICON_FILE data/icons/arqiver.svg)

set(EXTRA_HEADERS label.h lineedit.h)

# translations
if(Qt6LinguistTools_FOUND)
  file(GLOB TS_FILES data/translations/arqiver_*.ts)
  qt6_add_translation(QM_FILES ${TS_FILES})
  add_executable(arqiver ${arqiver_SRCS} ${arqiver_RESOURCES} ${EXTRA_HEADERS} ${DESKTOP_FILE} ${QM_FILES})
  install(FILES ${QM_FILES} DESTINATION "${CMAKE_INSTALL_PREFIX}/share/arqiver/translations")
else(Qt6LinguistTools_FOUND)
  add_executable(arqiver ${arqiver_SRCS} ${arqiver_RESOURCES} ${EXTRA_HEADERS} ${DESKTOP_FILE})
endif(Qt6LinguistTools_FOUND)

target_link_libraries(arqiver ${Qt6Core_LIBRARIES}
                              ${Qt6Gui_LIBRARIES}
                              ${Qt6Widgets_LIBRARIES}
                              ${Qt6Svg_LIBRARIES}
                              ${Qt6DBus_LIBRARIES})

# installation
install(TARGETS arqiver RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
install(FILES ${DESKTOP_FILE} DESTINATION "${CMAKE_INSTALL_PREFIX}/share/applications")
install(FILES ${ICON_FILE} DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps")
