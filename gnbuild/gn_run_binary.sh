#!/bin/bash

# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# Helper script to run an arbitrary binary produced by the current build.

"./${1}" "${@:2}"
