# Lotus Petal — ESP-IDF project for the reference round knob-and-touch board.
#
#   . $IDF_PATH/export.sh
#   idf.py set-target esp32s3
#   idf.py build
#
# See ../docs/BUILD.md for the toolchain requirements (ESP-IDF 5.4 or newer) and
# ../docs/PORTING.md for taking this to different hardware.
cmake_minimum_required(VERSION 3.16)

# App version — THE single source of truth. It goes into the firmware descriptor,
# and the About screen reads it back from there at runtime, so there is nowhere else
# to update. Bump the MINOR on every change; the major only on request.
set(PROJECT_VER "4.2")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lotus-petal)
