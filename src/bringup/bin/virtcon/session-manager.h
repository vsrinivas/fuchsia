// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_VIRTCON_SESSION_MANAGER_H_
#define SRC_BRINGUP_BIN_VIRTCON_SESSION_MANAGER_H_

#include <fuchsia/virtualconsole/llcpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/status.h>

#include "src/bringup/bin/virtcon/vc.h"

namespace virtcon {

// This is the class that accepts virtcon session's from virtcon's FIDL and manages
// the sessions until they are closed.
// This class is not thread safe.
class SessionManager final : public llcpp::fuchsia::virtualconsole::SessionManager::Interface {
 public:
  // Create a SessionManager. The pointers to `dispatcher` and `color_scheme` are unowned and
  // must outlive the SessionManager class.
  SessionManager(async_dispatcher_t* dispatcher, bool keep_log_visible,
                 const color_scheme_t* color_scheme)
      : dispatcher_(dispatcher), keep_log_visible_(keep_log_visible), color_scheme_(color_scheme) {}

  zx_status_t Bind(zx::channel request);

  // FIDL functions.
  void CreateSession(::zx::channel session, CreateSessionCompleter::Sync& completer) override;
  void HasPrimaryConnected(HasPrimaryConnectedCompleter::Sync& completer) override;

  // Create a Vc Session that reads and writes from `session`.
  // The `session` channel must represent a connection to a `fuchsia.hardware.pty.Device`.
  // The returned `vc_t*` will be freed when the other end of `session` is closed.
  // For this reason the returned `vc_t*` can ONLY be used when the code has control over
  // the other end of the session, and it must be used with care.
  zx::status<vc_t*> CreateSession(zx::channel session);

 private:
  void SessionIoCallback(vc_t* vc, async_dispatcher_t* dispatcher, async::Wait* wait,
                         zx_status_t status, const zx_packet_signal_t* signal);

  zx::status<vc_t*> CreateSession(zx::channel session, bool make_active,
                                  const color_scheme_t* color_scheme);

  // The number of active vcs at the moment.
  uint32_t num_vcs_ = 0;
  async_dispatcher_t* dispatcher_;
  const bool keep_log_visible_;
  const color_scheme_t* color_scheme_;
};

}  // namespace virtcon

#endif  // SRC_BRINGUP_BIN_VIRTCON_SESSION_MANAGER_H_
