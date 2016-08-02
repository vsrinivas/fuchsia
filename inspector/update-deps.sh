#!/bin/sh
# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

rm -rf bower_components
bower install
vulcanize --inline-scripts --inline-css --strip-comments elements/dependencies.html > elements/dependencies.vulcanized.html
rm -rf bower_components
