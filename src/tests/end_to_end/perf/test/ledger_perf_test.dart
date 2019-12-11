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
    'add_new_page_after_clear.tspec',
    'add_new_page_precached.tspec',
    'add_new_page.tspec',
    'get_same_page.tspec',
    'get_page_id.tspec',
    'get_small_entry.tspec',
    'get_small_entry_inline.tspec',
    'get_big_entry.tspec',
    'put.tspec',
    'put_as_reference.tspec',
    'put_big_entry.tspec',
    'transaction.tspec',
    'update_entry.tspec',
    'update_big_entry.tspec',
    'update_entry_transactions.tspec',
    'delete_entry.tspec',
    'delete_big_entry.tspec',
    'delete_entry_transactions.tspec',
    'disk_space_empty_ledger.tspec',
    'disk_space_empty_pages.tspec',
    'disk_space_entries.tspec',
    'disk_space_small_keys.tspec',
    'disk_space_updates.tspec',
    'disk_space_one_commit_per_entry.tspec',
    'disk_space_cleared_page.tspec',
    'put_memory.tspec',
    'stories_single_active.tspec',
    'stories_many_active.tspec',
    'stories_wait_cached.tspec',
    'stories_memory.tspec',
  ];
  for (final specFile in ledgerTests) {
    addTspecTest('/pkgfs/packages/ledger_benchmarks/0/data/$specFile');
  }
}
