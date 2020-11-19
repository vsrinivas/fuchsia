# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

OUTFILE=$1
GIT_DIR=$2

VERSION_INFO="$(TZ=UTC git --git-dir=$GIT_DIR show --no-patch --no-notes --pretty='%H-%ct' HEAD 2> /dev/null)"

# Update the existing file only if it's changed.
if [ ! -r "$OUTFILE" ] || [ "$(<"$OUTFILE")" != "$VERSION_INFO" ]; then
  echo "$VERSION_INFO" > "$OUTFILE"
fi