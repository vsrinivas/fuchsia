#!/bin/sh

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Helper script to run a given command and then produce a stamp file.

stamp_file="$1"
shift
"$@" && touch "$stamp_file"
