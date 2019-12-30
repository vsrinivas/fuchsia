// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  // Run "local" Ledger benchmarks.  These don't need external services to
  // function properly.
  //
  // This list should be kept in sync with the list in
  // peridot/tests/benchmarks/benchmarks.cc until that list is removed
  // (TODO(fxb/23091)).
  const ledgerTests = [
    'stories_single_active.tspec',
    'stories_many_active.tspec',
    'stories_wait_cached.tspec',
  ];
  for (final specFile in ledgerTests) {
    addTspecTest('/pkgfs/packages/ledger_benchmarks/0/data/$specFile');
  }
}
