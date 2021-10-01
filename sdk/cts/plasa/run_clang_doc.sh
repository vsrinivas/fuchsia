#!/bin/sh

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Runs the supplied command line and updates a stamp file on success.
#
# Parameters:
# $1: the filename of the stamp file.
# $2: the directory name of the output directory.
# $3: the output file for clang output.
# $4...: the command line to run.

_stamp_file="$1"
shift
_output_dir="$1"
shift
_diagnosis_file="$1"
shift
# Create the output dir anyways, since it's not always created.
touch ${_diagnosis_file} && \
  mkdir -p "${_output_dir}" && \
  ${@} 2>&1 &>"${_diagnosis_file}" && \
  touch "${_stamp_file}"
