#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

function usage {
  cat <<END
usage: bootstrap

Bootstrap the Platform Source Tree.
END
}

if [[ $# -gt 0 ]]; then
  usage
  exit 1
fi

# The fetched script will
# - create "fuchsia" directory if it does not exist,
# - download "jiri" command to "fuchsia/.jiri_root/bin"
curl -s "https://fuchsia.googlesource.com/jiri/+/HEAD/scripts/bootstrap_jiri?format=TEXT" | base64 --decode | bash -s fuchsia
cd fuchsia

.jiri_root/bin/jiri import -name=integration flower https://fuchsia.googlesource.com/integration
.jiri_root/bin/jiri update

echo "Done creating a Platform Source Tree at \"$(pwd)\"."
echo "Recommended: export PATH=\"$(pwd)/.jiri_root/bin:\$PATH\""
