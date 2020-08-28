// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_MANAGER_H_
#define SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_MANAGER_H_

#include <fuchsia/exception/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/exception.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>

#include <deque>
#include <vector>

#include "src/developer/forensics/exceptions/pending_exception.h"
#include "src/developer/forensics/exceptions/process_handler.h"

namespace forensics {
namespace exceptions {

class HandlerManager {
 public:
  HandlerManager(async_dispatcher_t* dispatcher, size_t max_num_handlers,
                 zx::duration exception_ttl);

  // Spawns a dedicated handler for |exception|. This way if the exception handling logic
  // were to crash, e.g., while generating the minidump from the process, only the sub-process would
  // be in an exception and exceptions.cmx could still handle exceptions in separate sub-processes.
  void Handle(zx::exception exception);

 private:
  void HandleNextPendingException();

  async_dispatcher_t* dispatcher_;

  zx::duration exception_ttl_;
  std::deque<PendingException> pending_exceptions_;

  // The |max_num_handlers| handlers.
  std::vector<ProcessHandler> handlers_;

  // List of indexes from 0 to |max_num_handlers|-1 of available handlers among |handlers_|.
  std::deque<size_t> available_handlers_;
};

}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_MANAGER_H_
