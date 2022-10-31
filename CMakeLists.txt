project(arqiver)
cmake_minimum_required(VERSION 3.0)
set(CMAKE_BUILD_TYPE Release)
add_definitions(-Wall)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr")
endif()

option(ENABLE_QT5 "Building Qt5 plugin." ON)

set(QT5_MINIMUM_VERSION "5.12.0")
set(QT6_MINIMUM_VERSION "6.3.0")

if(NOT ENABLE_QT5)
  set(QT_VERSION_MAJOR 6)
  find_package(Qt6 "${QT6_MINIMUM_VERSION}" REQUIRED COMPONENTS Core Gui Widgets Svg DBus)
else()
  set(QT_VERSION_MAJOR 5)
  find_package(Qt5 "${QT5_MINIMUM_VERSION}" REQUIRED COMPONENTS Core Gui Widgets Svg DBus)
endif()

# optional localization
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS LinguistTools)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
                    ${Qt${QT_VERSION_MAJOR}Core_INCLUDE_DIRS}
                    ${Qt${QT_VERSION_MAJOR}Gui_INCLUDE_DIRS}
                    ${Qt${QT_VERSION_MAJOR}Widgets_INCLUDE_DIRS}
                    ${Qt${QT_VERSION_MAJOR}Svg_INCLUDE_DIRS}
                    ${Qt${QT_VERSION_MAJOR}DBus_INCLUDE_DIRS})

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

if(QT_VERSION_MAJOR EQUAL 6)
  set(EXTRA_HEADERS label.h lineedit.h)
else()
  if(CMAKE_VERSION VERSION_LESS "3.10.0") # CMP0071 in 3.10: Let AUTOMOC and AUTOUIC process GENERATED files
    qt5_wrap_cpp(EXTRA_HEADERS label.h lineedit.h)
  else()
    set(EXTRA_HEADERS label.h lineedit.h)
  endif()
endif()

# translations
if(Qt${QT_VERSION_MAJOR}LinguistTools_FOUND)
  file(GLOB TS_FILES data/translations/arqiver_*.ts)
  if(QT_VERSION_MAJOR EQUAL 6)
    qt6_add_translation(QM_FILES ${TS_FILES})
  else()
    qt5_add_translation(QM_FILES ${TS_FILES})
  endif()
  add_executable(arqiver ${arqiver_SRCS} ${arqiver_RESOURCES} ${EXTRA_HEADERS} ${DESKTOP_FILE} ${QM_FILES})
  install(FILES ${QM_FILES} DESTINATION "${CMAKE_INSTALL_PREFIX}/share/arqiver/translations")
else(Qt${QT_VERSION_MAJOR}LinguistTools_FOUND)
  add_executable(arqiver ${arqiver_SRCS} ${arqiver_RESOURCES} ${EXTRA_HEADERS} ${DESKTOP_FILE})
endif(Qt${QT_VERSION_MAJOR}LinguistTools_FOUND)

target_link_libraries(arqiver ${Qt${QT_VERSION_MAJOR}Core_LIBRARIES}
                              ${Qt${QT_VERSION_MAJOR}Gui_LIBRARIES}
                              ${Qt${QT_VERSION_MAJOR}Widgets_LIBRARIES}
                              ${Qt${QT_VERSION_MAJOR}Svg_LIBRARIES}
                              ${Qt${QT_VERSION_MAJOR}DBus_LIBRARIES})

# installation
install(TARGETS arqiver RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
install(FILES ${DESKTOP_FILE} DESTINATION "${CMAKE_INSTALL_PREFIX}/share/applications")
install(FILES ${ICON_FILE} DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps")
