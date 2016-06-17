#!/bin/bash

# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# Generate a linker script by substituting all variables with provided values.

declare PATTERN=""

for i in "$@"; do
  case ${i} in
    *=*)
      PATTERN="${PATTERN}s/%${i%=*}%/${i#*=}/;"
      ;;
  esac
done

sed "${PATTERN}" <$1 >$2
