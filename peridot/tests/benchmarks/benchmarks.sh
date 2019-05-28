#!/boot/bin/sh
#
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script launches the peridot benchmarks binary, which runs all benchmarks
# for the peridot layer.

/pkgfs/packages/fuchsia_benchmarks/0/bin/fuchsia_benchmarks "$@"
