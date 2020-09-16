#!/bin/sh

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script gets an estimate for the time of the most recent commit from the
# integration repo. This is used at runtime as a known-good lower bound on what
# the current time is for cases in which the clock hasn't yet been updated from
# the network.
#
# We do this by taking the CommitDate of the most recent commit from the
# integration repo. Unlike commits in other repositories, the commits in the
# integration repo are always created by server infrastructure, and thus their
# CommitDate fields are more reliable. This is critical since a backstop time
# which is erroneously in the future could break many applications.

set -eu

# The path of the integration repo.
INTEGRATION="$1"
# A path to populate with the latest commit date as an ISO 8601 timestamp.
OUTPUT="$2"
# A path to populate with the latest commit date as seconds since unix epoch.
UNIX_OUTPUT="$3"

# Set the following options to make the output as stable as possible:
# - GIT_CONFIG_NOSYSTEM=1   - Don't check /etc/gitconfig
# - --date=iso-strict-local - Format date as a strict ISO 8601-format timestamp
# - TZ=UTC                  - Set the local timezone to UTC; when paired with the
#                             "-local" suffix to "iso-strict", which tells Git to
#                             use the local timezone, it has the effect of
#                             formatting the time in the UTC timezone
# - --format=%cd            - Print the CommitDate field only, respecting the
#                             formatting given by the --date flag
GIT_CONFIG_NOSYSTEM=1 TZ=UTC git --git-dir="$INTEGRATION"/.git log --date=iso-strict-local --format=%cd -n 1 > "$OUTPUT"
GIT_CONFIG_NOSYSTEM=1 TZ=UTC git --git-dir="$INTEGRATION"/.git log --date=unix --format=%cd -n 1 > "$UNIX_OUTPUT"
