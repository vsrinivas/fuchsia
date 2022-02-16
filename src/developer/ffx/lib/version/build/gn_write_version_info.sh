#!/bin/sh
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
OUTFILE="$1"
GIT_DIR="$2"

if [ ! -d "$GIT_DIR" ]; then
  echo >&2 "Invalid GIT_DIR provided: $GIT_DIR"
fi

# The --no-optional-locks is used to ensure that git read-only
# operations do not modify the index (https://fxbug.dev/93875).
GIT_REV="$(git --no-optional-locks --git-dir=${GIT_DIR} rev-parse HEAD 2>/dev/null)"
VERSION_INFO="$(TZ=UTC git --no-optional-locks --git-dir=${GIT_DIR} show --no-patch --no-notes --pretty='%H-%ct' ${GIT_REV} 2> /dev/null)"
if [ -z "$VERSION_INFO" ]; then
  echo >&2 "Failed to gather version information from ${GIT_DIR}"
  exit 1
fi

# Update the existing file only if it's changed.
if [ ! -r "$OUTFILE" ] || [ "$(<"$OUTFILE")" != "$VERSION_INFO" ]; then
  echo "$VERSION_INFO" > "$OUTFILE"
fi
