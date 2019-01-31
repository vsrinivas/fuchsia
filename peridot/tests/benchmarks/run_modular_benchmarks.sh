#!/boot/bin/sh
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script runs the Modular benchmarks. It's intended for use in continuous
# integration jobs. It must be run from the pathname to the file inside the
# package, it cannot (yet) be run by the package name alone.

set -e

trace record --spec-file=/pkgfs/packages/modular_benchmarks/0/data/modular_benchmark_story.tspec

# add more benchmark tests here
