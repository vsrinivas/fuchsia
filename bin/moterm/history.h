// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOTERM_HISTORY_H_
#define APPS_MOTERM_HISTORY_H_

#include "apps/ledger/services/public/ledger.fidl.h"
#include "lib/ftl/macros.h"

namespace moterm {

// Ledger-backed store for terminal history.
class History {
 public:
  History();
  ~History();

  // TODO(ppi): drop this once FW-97 is fixed, at which point the PagePtr can be
  // just passed in the constructor.
  void Initialize(ledger::PagePtr page);

  // Retrieves the list of history commands, ordered from oldest to newest.
  void ReadEntries(std::function<void(std::vector<std::string>)> callback);

  // Adds the given command to the terminal history.
  void AddEntry(const std::string& entry);

 private:
  void DoReadEntries(std::function<void(std::vector<std::string>)> callback);

  // Ensures that the number of commands in terminal history does not exceed the
  // maximum size by removing the oldest entries.
  void Trim();

  bool initialized_ = false;
  ledger::PagePtr page_;
  std::vector<std::function<void(std::vector<std::string>)>>
      pending_read_entries_;

  FTL_DISALLOW_COPY_AND_ASSIGN(History);
};

}  // namespace moterm

#endif  // APPS_MOTERM_HISTORY_H_
