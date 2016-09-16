#!/bin/bash

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
set -eu

# Fetch dependencies
cd $GOPATH/src/fuchsia.googlesource.com/thinfs
go get -u github.com/Masterminds/glide
export PATH=$PATH:$GOPATH/bin/
glide install
