#!/boot/bin/sh
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script runs the Modular benchmarks. It's intended for use in continuous
# integration jobs.

set -e

/system/bin/trace record --spec-file=/system/test/modular_tests/modular_benchmark_story.tspec
# add more benchmark tests here
