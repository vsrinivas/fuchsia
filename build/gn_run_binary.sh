#!/bin/sh

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Helper script to run an arbitrary binary produced by the current build.
# The first argument is the bin directory of the toolchain, where
# llvm-symbolizer can be found.  The second argument is the binary to run,
# and remaining arguments are passed to that binary.

clang_bindir="$1"
shift

binary="$1"
shift

case "$binary" in
/*) ;;
*) binary="./$binary" ;;
esac

# Make sure any sanitizer runtimes that might be included in the binary
# can find llvm-symbolizer.
symbolizer="${clang_bindir}/llvm-symbolizer"
export ASAN_SYMBOLIZER_PATH="$symbolizer"
export LSAN_SYMBOLIZER_PATH="$symbolizer"
export MSAN_SYMBOLIZER_PATH="$symbolizer"
export UBSAN_SYMBOLIZER_PATH="$symbolizer"
export TSAN_OPTIONS="$TSAN_OPTIONS external_symbolizer_path=$symbolizer"

# This tells it to look for ./.build-id/xx/xxx.debug files.
# It can thus find the unstripped binary for the stripped binary being run.
export LLVM_SYMBOLIZER_OPTS=--debug-file-directory=.

exec "$binary" ${1+"$@"}
