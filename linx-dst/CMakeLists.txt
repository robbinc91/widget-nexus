cmake_minimum_required(VERSION 3.10)
project(widget-nexus-lnx CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

pkg_check_modules(APPINDICATOR ayatana-appindicator3-0.1)
if(APPINDICATOR_FOUND)
  add_compile_definitions(HAVE_AYATANA_APPINDICATOR)
endif()

add_executable(widget-nexus-lnx
  src/main.cpp
  src/model.cpp
)

target_include_directories(widget-nexus-lnx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${GTK3_INCLUDE_DIRS})
target_compile_options(widget-nexus-lnx PRIVATE ${GTK3_CFLAGS_OTHER})
target_link_libraries(widget-nexus-lnx PRIVATE ${GTK3_LIBRARIES})

if(APPINDICATOR_FOUND)
  target_include_directories(widget-nexus-lnx PRIVATE ${APPINDICATOR_INCLUDE_DIRS})
  target_compile_options(widget-nexus-lnx PRIVATE ${APPINDICATOR_CFLAGS_OTHER})
  target_link_libraries(widget-nexus-lnx PRIVATE ${APPINDICATOR_LIBRARIES})
endif()
