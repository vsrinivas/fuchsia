#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Helper script to run an arbitrary binary produced by the current build.

"./${1}" "${@:2}"
