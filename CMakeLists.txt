project(arqiver)
cmake_minimum_required(VERSION 3.0)
set(CMAKE_BUILD_TYPE Release)
add_definitions(-Wall)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr")
endif()

set(QT_MINIMUM_VERSION "5.12.0")

find_package(Qt5Core "${QT_MINIMUM_VERSION}" REQUIRED)
find_package(Qt5Gui "${QT_MINIMUM_VERSION}" REQUIRED)
find_package(Qt5Widgets "${QT_MINIMUM_VERSION}" REQUIRED)
find_package(Qt5Svg "${QT_MINIMUM_VERSION}" REQUIRED)

# optional localization
find_package(Qt5LinguistTools QUIET)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
                    ${Qt5Core_INCLUDE_DIRS}
                    ${Qt5Gui_INCLUDE_DIRS}
                    ${Qt5Widgets_INCLUDE_DIRS}
                    ${Qt5Svg_INCLUDE_DIRS})

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

if(CMAKE_VERSION VERSION_LESS "3.10.0") # CMP0071 in 3.10: Let AUTOMOC and AUTOUIC process GENERATED files
  qt5_wrap_cpp(EXTRA_HEADERS label.h lineedit.h)
else()
  set(EXTRA_HEADERS label.h lineedit.h)
endif()

# translations
if(Qt5LinguistTools_FOUND)
  file(GLOB TS_FILES data/translations/arqiver_*.ts)
  qt5_add_translation(QM_FILES ${TS_FILES})
  add_executable(arqiver ${arqiver_SRCS} ${arqiver_RESOURCES} ${EXTRA_HEADERS} ${DESKTOP_FILE} ${QM_FILES})
  install(FILES ${QM_FILES} DESTINATION "${CMAKE_INSTALL_PREFIX}/share/arqiver/translations")
else(Qt5LinguistTools_FOUND)
  add_executable(arqiver ${arqiver_SRCS} ${arqiver_RESOURCES} ${EXTRA_HEADERS} ${DESKTOP_FILE})
endif(Qt5LinguistTools_FOUND)

target_link_libraries(arqiver ${Qt5Core_LIBRARIES}
                              ${Qt5Gui_LIBRARIES}
                              ${Qt5Widgets_LIBRARIES}
                              ${Qt5Svg_LIBRARIES})

# installation
install(TARGETS arqiver RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
install(FILES ${DESKTOP_FILE} DESTINATION "${CMAKE_INSTALL_PREFIX}/share/applications")
install(FILES ${ICON_FILE} DESTINATION "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps")
