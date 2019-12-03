// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTS_INTEGRATION_TEST_PAGE_WATCHER_H_
#define SRC_LEDGER_BIN_TESTS_INTEGRATION_TEST_PAGE_WATCHER_H_

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/optional.h>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/tests/integration/integration_test.h"
#include "src/lib/callback/capture.h"

namespace ledger {

class TestPageWatcher : public PageWatcher {
 public:
  explicit TestPageWatcher(
      fidl::InterfaceRequest<PageWatcher> request, fit::closure change_callback = [] {});

  void DelayCallback(bool delay_callback);
  void CallOnChangeCallback();
  uint GetChangesSeen();
  ResultState GetLastResultState();
  PageSnapshotPtr* GetLastSnapshot();
  const PageChange& GetLastPageChange();

 private:
  // PageWatcher:
  void OnChange(PageChange page_change, ResultState result_state,
                OnChangeCallback callback) override;

  uint changes_seen_ = 0;
  ResultState last_result_state_;
  PageSnapshotPtr last_snapshot_;
  PageChange last_page_change_;

  fidl::Binding<PageWatcher> binding_;
  bool delay_callback_ = false;
  OnChangeCallback on_change_callback_;
  fit::closure change_callback_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTS_INTEGRATION_TEST_PAGE_WATCHER_H_
