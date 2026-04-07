cmake_minimum_required(VERSION 3.14)
project(btd700ctl
    VERSION 0.1
    DESCRIPTION "unofficial driver for Sennheiser BTD 700 USB bluetooth dongle"
    LANGUAGES C CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED hidapi-hidraw)

option(BUILD_SHARED_LIBS "Build shared library instead of static" OFF)

add_library(btd700ctl
    src/btd700.c
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(btd700ctl PRIVATE BTD700CTL_BUILD)
endif()

target_include_directories(btd700ctl
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${HIDAPI_INCLUDE_DIRS}
)

target_link_libraries(btd700ctl PRIVATE ${HIDAPI_LIBRARIES})

target_compile_features(btd700ctl PRIVATE c_std_99)

add_library(btd700ctl::btd700ctl ALIAS btd700ctl)

include(GNUInstallDirs)

install(TARGETS btd700ctl
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/btd700
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# example app
add_executable(btd700_example example.cpp)
target_link_libraries(btd700_example PRIVATE btd700ctl::btd700ctl)

# daemon
add_executable(btd700d daemon/btd700d.c)
target_link_libraries(btd700d PRIVATE btd700ctl::btd700ctl)
target_compile_features(btd700d PRIVATE c_std_99)

install(TARGETS btd700d RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES daemon/btd700d.service DESTINATION lib/systemd/user)

