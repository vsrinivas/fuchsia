// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_VIRTCON_SESSION_MANAGER_H_
#define SRC_BRINGUP_BIN_VIRTCON_SESSION_MANAGER_H_

#include <fuchsia/virtualconsole/llcpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/status.h>

#include "src/bringup/bin/virtcon/vc.h"

class SessionManager final : public llcpp::fuchsia::virtualconsole::SessionManager::Interface {
 public:
  // Create a SessionManager. The pointers to `dispatcher` and `color_scheme` are unowned and
  // must outlive the SessionManager class.
  SessionManager(async_dispatcher_t* dispatcher, bool keep_log_visible,
                 const color_scheme_t* color_scheme)
      : dispatcher_(dispatcher), keep_log_visible_(keep_log_visible), color_scheme_(color_scheme) {}

  zx_status_t Bind(zx::channel request);

  zx_status_t CreateSession(zx::channel session);

  // FIDL functions.
  void CreateSession(::zx::channel session, CreateSessionCompleter::Sync& completer) override;
  void HasPrimaryConnected(HasPrimaryConnectedCompleter::Sync& completer) override;

  // For Tests only. Similar to CreateSession but it returns an unowned pointer to the created vc.
  // The created vc will be freed when the other channel end of `session` is closed.
  zx::status<vc_t*> CreateSessionForTest(zx::channel session);

 private:
  // Create a Vc Session that reads and writes from `session`.
  // The `session` channel must represent a connection to a `fuchsia.hardware.pty.Device`.
  // The returned `vc_t*` will be freed when the other end of `session` is closed.
  // For this reason it cannot be used outside of tests that control the other end of `session`.
  zx::status<vc_t*> CreateSession(zx::channel session, bool make_active,
                                  const color_scheme_t* color_scheme);

  async_dispatcher_t* dispatcher_;
  const bool keep_log_visible_;
  const color_scheme_t* color_scheme_;
};

#endif  // SRC_BRINGUP_BIN_VIRTCON_SESSION_MANAGER_H_
