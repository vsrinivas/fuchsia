// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'perf_test.dart';

void main() {
  enableLoggingOutput();

  // Run "local" Ledger benchmarks.  These don't need external services to
  // function properly.
  //
  // TODO(fxb/23091): For now we are just running one test case here because
  // running the full set crosses the timeout time limit.  When we figure out
  // how to address that, we should add the full list from
  // peridot/tests/benchmarks/benchmarks.cc.
  const ledgerTests = [
    'add_new_page_after_clear.tspec',
  ];
  for (final specFile in ledgerTests) {
    addTspecTest('/pkgfs/packages/ledger_benchmarks/0/data/$specFile');
  }
}
