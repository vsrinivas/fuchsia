#!/usr/bin/env bash

# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -eo pipefail

# Create an empty block file to use as the stateful data partition. This is
# temporary until we can put this image on the Fuchsia data partition at
# runtime.
mkdir -p $(dirname ${1})
truncate --size=3800M "${1}"
