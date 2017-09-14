// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOTERM_SHELL_CONTROLLER_H_
#define APPS_MOTERM_SHELL_CONTROLLER_H_

#include <deque>
#include <map>
#include <string>
#include <vector>

#include <zx/channel.h>
#include <zx/vmo.h>

#include "garnet/bin/moterm/history.h"
#include "lib/fidl/c/waiter/async_waiter.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/fxl/macros.h"
#include "lib/fsl/io/redirection.h"
#include "lib/fsl/vmo/strings.h"

namespace moterm {

// Implements the controller protocol of the default shell. The controller
// exchanges control messages with the shell over an zx::channel.
//
// For the protocol description, see
// zircon/third_party/uapp/dash/src/controller.h.
class ShellController : public History::Client {
 public:
  ShellController(History* history);
  ~ShellController() override;

  // Returns the system command for starting the default shell.
  std::vector<std::string> GetShellCommand();

  // Returns the startup handles needed for initializing the default shell.
  std::vector<fsl::StartupHandle> GetStartupHandles();

  // Starts the communication with shell.
  void Start();

  // Stops communication with the shell.
  void Terminate();

  // History::Client:
  void OnRemoteEntry(const std::string& entry) override;

 private:
  bool SendBackHistory(std::vector<std::string> entries);
  void HandleAddToHistory(const std::string& entry);

  void ReadCommand();
  void WaitForShell();
  static void WaitComplete(zx_status_t result,
                           zx_signals_t pending,
                           uint64_t count,
                           void* context);

  // Ledger-backed store for terminal history.
  History* history_;

  const FidlAsyncWaiter* waiter_ = fidl::GetDefaultAsyncWaiter();
  FidlAsyncWaitID wait_id_ = 0;

  zx::channel channel_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ShellController);
};

}  // namespace moterm

#endif  // APPS_MOTERM_SHELL_CONTROLLER_H_
