# Lotus Petal — ESP-IDF project for the reference round knob-and-touch board.
#
#   . $IDF_PATH/export.sh
#   idf.py set-target esp32s3
#   idf.py build
#
# See ../docs/BUILD.md for the toolchain requirements (ESP-IDF 5.4 or newer) and
# ../docs/PORTING.md for taking this to different hardware.
cmake_minimum_required(VERSION 3.16)

# App version. Ends up in the firmware descriptor and on the About screen; keep
# it in step with UI_APP_VERSION in main/app/ui/ui_common.h.
set(PROJECT_VER "4.1")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lotus-petal)
