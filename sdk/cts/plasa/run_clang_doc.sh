#!/bin/sh

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Runs the supplied command line and updates a stamp file on success.
#
# Parameters:
# $1: the filename of the stamp file.
# $2...: the command line to run.

_stamp_file="$1"
shift
${@} &> /dev/null && touch "${_stamp_file}"
