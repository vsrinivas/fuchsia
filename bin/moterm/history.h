// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOTERM_HISTORY_H_
#define APPS_MOTERM_HISTORY_H_

#include <string>
#include <unordered_set>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"

namespace moterm {

// Ledger-backed store for terminal history.
class History : public ledger::PageWatcher {
 public:
  class Client {
   public:
    Client() {}
    virtual ~Client() {}

    virtual void OnRemoteEntry(const std::string& entry) = 0;

   private:
    FXL_DISALLOW_COPY_AND_ASSIGN(Client);
  };

  History();
  ~History() override;

  // TODO(ppi): drop this once FW-97 is fixed, at which point the PagePtr can be
  // just passed in the constructor.
  void Initialize(ledger::PagePtr page);

  // Retrieves the initial list of history commands, ordered from oldest to
  // newest.  This can currently be called only once, ie. does not support
  // multiple terminal views rendered by one application instance.
  // TODO(ppi): fix this once FW-97 is fixed, at which point we can just create
  // one instance of History per ShellController.
  void ReadInitialEntries(
      std::function<void(std::vector<std::string>)> callback);

  // Adds the given command to the terminal history.
  void AddEntry(const std::string& entry);

  void RegisterClient(Client* client);

  void UnregisterClient(Client* client);

 private:
  // ledger::PageWatcher:
  void OnChange(ledger::PageChangePtr page_change,
                ledger::ResultState result_state,
                const OnChangeCallback& callback) override;

  void DoReadEntries(std::function<void(std::vector<std::string>)> callback);

  // Ensures that the number of commands in terminal history does not exceed the
  // maximum size by removing the oldest entries.
  void Trim();

  std::unordered_set<Client*> clients_;
  bool initialized_ = false;
  ledger::PageSnapshotPtr snapshot_;
  fidl::Binding<ledger::PageWatcher> page_watcher_binding_;
  ledger::PagePtr page_;
  std::vector<std::function<void(std::vector<std::string>)>>
      pending_read_entries_;
  std::unordered_set<std::string> local_entry_keys_;

  FXL_DISALLOW_COPY_AND_ASSIGN(History);
};

}  // namespace moterm

#endif  // APPS_MOTERM_HISTORY_H_
