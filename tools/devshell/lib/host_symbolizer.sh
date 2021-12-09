# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

symbolizer="${PREBUILT_CLANG_DIR}/bin/llvm-symbolizer"
export ASAN_SYMBOLIZER_PATH="$symbolizer"
export LSAN_SYMBOLIZER_PATH="$symbolizer"
export MSAN_SYMBOLIZER_PATH="$symbolizer"
export UBSAN_SYMBOLIZER_PATH="$symbolizer"
export TSAN_OPTIONS="$TSAN_OPTIONS external_symbolizer_path=$symbolizer"

# This tells it to look for ${FUCHSIA_BUILD_DIR}/.build-id/xx/xxx.debug files.
# It can thus find the unstripped binary for the stripped binary being run.
export LLVM_SYMBOLIZER_OPTS="--debug-file-directory=${FUCHSIA_BUILD_DIR}"
