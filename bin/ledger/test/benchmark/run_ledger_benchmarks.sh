#!/boot/bin/sh

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script runs the Ledger benchmarks. It's intended for use in continuous
# integration jobs.

set -e

/system/bin/trace record --spec-file=/system/data/ledger/benchmark/put.tspec
/system/bin/trace record --spec-file=/system/data/ledger/benchmark/transaction.tspec
