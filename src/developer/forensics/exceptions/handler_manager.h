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

#include <cstdint>
#include <deque>
#include <map>
#include <memory>

#include "src/developer/forensics/exceptions/pending_exception.h"

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

  class Handler {
   public:
    Handler(async_dispatcher_t* dispatcher, zx::process subprocess,
            async::WaitOnce::Handler on_subprocess_exit);

   private:
    zx::process subprocess_;
    async::WaitOnce on_subprocess_exit_;
  };

  async_dispatcher_t* dispatcher_;
  size_t max_num_subprocesses_;
  zx::duration exception_ttl_;

  std::deque<PendingException> pending_exceptions_;

  size_t num_active_subprocesses_{0};

  std::map<size_t, std::unique_ptr<Handler>> handlers_;
  size_t next_handler_id_{0};
};

}  // namespace exceptions
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_EXCEPTIONS_HANDLER_MANAGER_H_
