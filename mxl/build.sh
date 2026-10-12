#!/usr/bin/env bash
# build.sh - compile mxl_writer and mxl_reader against your MXL build.
# Expects MXL cloned in ~/mxl and built with the Linux-GCC-Release preset.
# Override the location with:  MXL_DIR=/path/to/mxl ./build.sh
set -e
cd "$(dirname "$0")"
MXL_DIR=${MXL_DIR:-$HOME/mxl}
INC=$MXL_DIR/lib/include
LIB=$MXL_DIR/build/Linux-GCC-Release/lib
[ -f "$LIB/libmxl.so" ] || { echo "libmxl.so not found in $LIB - build MXL first"; exit 1; }
for p in mxl_writer mxl_reader; do
  gcc -O2 -Wall -Wextra -std=c11 -I "$INC" $p.c -L "$LIB" -lmxl -Wl,-rpath,"$LIB" -o $p
  echo "built mxl/$p"
done
