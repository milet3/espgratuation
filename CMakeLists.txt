# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Single source of truth for firmware version.
# Update only this value when preparing a new OTA package.
set(PROJECT_VER "4.1.1")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espgratuation)
