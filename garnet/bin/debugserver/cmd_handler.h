// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/fit/function.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"

namespace debugserver {

class RspServer;

// CommandHandler is responsible for handling GDB Remote Protocol commands.
class CommandHandler final {
 public:
  explicit CommandHandler(RspServer* server);
  ~CommandHandler() = default;

  // Handles the command packet |packet| of size |packet_size| bytes. Returns
  // false if the packet cannot be handled, otherwise returns true and calls
  // |callback|. Once a command is handled, |callback| will called with
  // |rsp| pointing to the contents of a response packet of size |rsp_size|
  // bytes. If |rsp_size| equals 0, then the response is empty.
  //
  // If this method returns false, then |callback| will never be called. If this
  // returns true, |callback| is guaranteed to be called exactly once.
  // |callback| can be called before HandleCommand returns.
  using ResponseCallback = fit::function<void(const fxl::StringView& rsp)>;
  bool HandleCommand(const fxl::StringView& packet, ResponseCallback callback);

 private:
  // Command handlers for each "letter" packet. We use underscores in the method
  // names to clearly delineate lowercase letters.
  bool HandleQuestionMark(ResponseCallback callback);
  bool Handle_c(const fxl::StringView& packet, ResponseCallback callback);
  bool Handle_C(const fxl::StringView& packet, ResponseCallback callback);
  bool Handle_D(const fxl::StringView& packet, ResponseCallback callback);
  bool Handle_g(ResponseCallback callback);
  bool Handle_G(const fxl::StringView& packet, ResponseCallback callback);
  bool Handle_H(const fxl::StringView& packet, ResponseCallback callback);
  bool Handle_m(const fxl::StringView& packet, ResponseCallback callback);
  bool Handle_M(const fxl::StringView& packet, ResponseCallback callback);
  bool Handle_q(const fxl::StringView& prefix, const fxl::StringView& params,
                ResponseCallback callback);
  bool Handle_Q(const fxl::StringView& prefix, const fxl::StringView& params,
                ResponseCallback callback);
  bool Handle_T(const fxl::StringView& packet, ResponseCallback callback);
  bool Handle_v(const fxl::StringView& packet, ResponseCallback callback);
  bool Handle_zZ(bool insert, const fxl::StringView& packet,
                 ResponseCallback callback);

  // q/Q packets:
  // qAttached
  bool HandleQueryAttached(const fxl::StringView& params,
                           ResponseCallback callback);
  // qC
  bool HandleQueryCurrentThreadId(const fxl::StringView& params,
                                  ResponseCallback callback);
  // qRcmd
  bool HandleQueryRcmd(const fxl::StringView& command,
                       ResponseCallback callback);
  // qSupported
  bool HandleQuerySupported(const fxl::StringView& params,
                            ResponseCallback callback);
  // qfThreadInfo and qsThreadInfo
  bool HandleQueryThreadInfo(bool is_first, ResponseCallback callback);
  // qXfer
  bool HandleQueryXfer(const fxl::StringView& params,
                       ResponseCallback callback);
  // QNonStop
  bool HandleSetNonStop(const fxl::StringView& params,
                        ResponseCallback callback);

  // v packets:
  bool Handle_vAttach(const fxl::StringView& packet, ResponseCallback callback);
  bool Handle_vCont(const fxl::StringView& packet, ResponseCallback callback);
  bool Handle_vKill(const fxl::StringView& packet, ResponseCallback callback);
  bool Handle_vRun(const fxl::StringView& packet, ResponseCallback callback);

  // Breakpoints
  bool InsertSoftwareBreakpoint(uintptr_t addr, size_t kind,
                                const fxl::StringView& optional_params,
                                ResponseCallback callback);
  bool RemoveSoftwareBreakpoint(uintptr_t addr, size_t kind,
                                ResponseCallback callback);

  // The root Server instance that owns us.
  RspServer* server_;  // weak

  // Indicates whether we are currently in a qfThreadInfo/qsThreadInfo sequence.
  bool in_thread_info_sequence_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CommandHandler);
};

}  // namespace debugserver
