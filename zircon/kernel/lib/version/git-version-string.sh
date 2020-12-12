#!/bin/bash

# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

readonly OUTFILE="$1"
readonly CHECKOUT_DIR="$2"
readonly DIRTY_CHECK="$3"

set -e

GIT_REV="git-$(git -C "$CHECKOUT_DIR" rev-parse HEAD 2>/dev/null)"

if $DIRTY_CHECK && [ -n "$(git -C "$CHECKOUT_DIR" status --porcelain --untracked-files=no 2>/dev/null)" ]; then
  GIT_REV+="-dirty"
fi

# Update the existing file only if it's changed.
if [ ! -r "$OUTFILE" ] || [ "$(<"$OUTFILE")" != "$GIT_REV" ]; then
  # Make sure not to include a trailing newline!
  printf '%s' "$GIT_REV" > "$OUTFILE"
fi
