// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/virtcon/session-manager.h"

#include <fuchsia/hardware/pty/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/watcher.h>
#include <lib/zircon-internal/paths.h>
#include <sys/ioctl.h>

#include <fbl/unique_fd.h>

namespace virtcon {

namespace fpty = ::llcpp::fuchsia::hardware::pty;

namespace {

void SessionDestroy(vc_t* vc) {
  if (vc->io != nullptr) {
    fdio_unsafe_release(vc->io);
  }
  if (vc->proc != ZX_HANDLE_INVALID) {
    zx_task_kill(vc->proc);
  }
  if (vc->pty_wait) {
    vc->pty_wait.reset();
  }
  // vc_destroy() closes the vc->fd.
  vc_destroy(vc);
}

}  // namespace

void SessionManager::SessionIoCallback(vc_t* vc, async_dispatcher_t* dispatcher, async::Wait* wait,
                                       zx_status_t status, const zx_packet_signal_t* signal) {
  if (status == ZX_OK) {
    uint32_t pollevt = 0;
    fdio_unsafe_wait_end(vc->io, signal->observed, &pollevt);

    if (pollevt & POLLIN) {
      char data[1024] = {};
      ssize_t r = read(vc->fd, data, sizeof(data));
      if (r >= 0) {
        if (r > 0) {
          vc_write(vc, data, r, 0);
        }
        wait->Begin(dispatcher);
        return;
      }
    }
  }

  num_vcs_--;
  SessionDestroy(vc);
}

zx::status<vc_t*> SessionManager::CreateSession(zx::channel session, bool make_active,
                                                const color_scheme_t* color_scheme) {
  // Connect to the PTY service.  We have to do this dance rather than just
  // using open() because open() uses the DESCRIBE flag internally, and the
  // plumbing of the PTY service through svchost causes the DESCRIBE to get
  // consumed by the wrong code, resulting in the wrong NodeInfo being provided.
  // This manifests as a loss of fd signals.
  fbl::unique_fd fd;
  {
    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    status = fdio_service_connect("/svc/fuchsia.hardware.pty.Device", remote.release());
    if (status != ZX_OK) {
      return zx::error(status);
    }
    int raw_fd;
    status = fdio_fd_create(local.release(), &raw_fd);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    fd.reset(raw_fd);
    int flags = fcntl(fd.get(), F_GETFL);
    if (flags < 0) {
      return zx::error(ZX_ERR_IO);
    }
    if (fcntl(fd.get(), F_SETFL, flags | O_NONBLOCK) < 0) {
      return zx::error(ZX_ERR_IO);
    }
  }

  fdio_t* io = fdio_unsafe_fd_to_io(fd.get());
  if (io == nullptr) {
    return zx::error(ZX_ERR_INTERNAL);
  }

  auto result = fpty::Device::Call::OpenClient(zx::unowned_channel(fdio_unsafe_borrow_channel(io)),
                                               0, std::move(session));
  fdio_unsafe_release(io);
  if (result.status() != ZX_OK) {
    return zx::error(result.status());
  }
  if (result->s != ZX_OK) {
    return zx::error(result->s);
  }

  vc_t* vc;
  if (vc_create(&vc, color_scheme)) {
    return zx::error(ZX_ERR_INTERNAL);
  }

  zx_handle_t handle;
  zx_signals_t signals;
  fdio_unsafe_wait_begin(io, POLLIN | POLLRDHUP | POLLHUP, &handle, &signals);
  vc->pty_wait = std::make_unique<async::Wait>(
      handle, signals, 0,
      [this, vc](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                 const zx_packet_signal_t* signal) {
        SessionIoCallback(vc, dispatcher, wait, status, signal);
      });

  struct winsize wsz = {};
  wsz.ws_col = vc->columns;
  wsz.ws_row = vc->rows;
  ioctl(fd.get(), TIOCSWINSZ, &wsz);

  vc->io = fdio_unsafe_fd_to_io(fd.get());

  vc->fd = fd.release();
  if (make_active) {
    vc_set_active(-1, vc);
  }
  vc->pty_wait->Begin(dispatcher_);

  num_vcs_++;
  return zx::ok(vc);
}

zx_status_t SessionManager::Bind(zx::channel request) {
  auto result = fidl::BindServer(dispatcher_, std::move(request), this);
  if (!result.is_ok()) {
    return result.error();
  }
  return ZX_OK;
}

void SessionManager::CreateSession(::zx::channel session, CreateSessionCompleter::Sync& completer) {
  completer.Reply(CreateSession(std::move(session)).status_value());
}

zx::status<vc_t*> SessionManager::CreateSession(zx::channel session) {
  return CreateSession(std::move(session), !keep_log_visible_ && (num_vcs_ == 0), color_scheme_);
}

void SessionManager::HasPrimaryConnected(HasPrimaryConnectedCompleter::Sync& completer) {
  completer.Reply(is_primary_bound());
}

}  // namespace virtcon
