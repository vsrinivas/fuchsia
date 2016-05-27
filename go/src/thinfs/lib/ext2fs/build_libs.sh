#!/usr/bin/env bash
set -e

# We mainly need this script so that we can pass in -fPIC to the CFLAGS when building
# libext2fs.  Without it, the .a cannot be linked into a mojo executable.
cd ./third_party/e2fsprogs
./configure CFLAGS="-O2 -fPIC"
make libs
