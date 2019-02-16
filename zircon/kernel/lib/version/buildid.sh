#!/usr/bin/env bash

# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

GIT_REV="git-$(git rev-parse HEAD 2>/dev/null)"
# Is there a .git or revision?
if [ $? -eq 0 ]; then
    if [ -n "$(git status --porcelain 2>/dev/null)" ]; then
        GIT_REV+="-dirty"
    fi
else
    GIT_REV="unknown"
fi

if [ $# -eq 1 ]; then
  cat > "$1.new" <<END
#ifndef __BUILDID_H
#define __BUILDID_H
#define BUILDID "${GIT_REV}"
#endif
END
  # Update the existing file only if it's changed.
  if [ -r "$1" ] && cmp -s "$1.new" "$1"; then
    rm -f "$1.new"
  else
    mv -f "$1.new" "$1"
  fi
else
    echo "${GIT_REV}"
fi
