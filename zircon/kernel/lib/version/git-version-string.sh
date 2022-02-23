#!/bin/bash

# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

readonly OUTFILE="$1"
readonly CHECKOUT_ROOT="$2"

set -e

# The --no-optional-locks option ensures git read-only operations do
# not refresh the index (https://fxbug.dev/93875)
GIT_REV="git-$(git --no-optional-locks -C "$CHECKOUT_ROOT" rev-parse HEAD 2>/dev/null)"

if [ -n "$(git --no-optional-locks -C "$CHECKOUT_ROOT" status --porcelain --untracked-files=no 2>/dev/null)" ]; then
  GIT_REV+="-dirty"
fi

# Update the existing file only if it's changed.
if [ ! -r "$OUTFILE" ] || [ "$(<"$OUTFILE")" != "$GIT_REV" ]; then
  # Make sure not to include a trailing newline!
  printf '%s' "$GIT_REV" > "$OUTFILE"
fi
