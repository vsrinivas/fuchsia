#!/boot/bin/sh
#
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

/pkgfs/packages/fuchsia_benchmarks/0/bin/fuchsia_benchmarks --perfcompare_mode "$@"
