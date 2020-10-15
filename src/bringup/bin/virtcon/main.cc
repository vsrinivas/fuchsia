// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <fuchsia/hardware/pty/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/virtualconsole/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/watcher.h>
#include <lib/zircon-internal/paths.h>
#include <lib/zx/channel.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/vfs.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <zircon/syscalls/object.h>

#include <iterator>
#include <memory>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string_piece.h>
#include <fbl/unique_fd.h>
#include <src/storage/deprecated-fs-fidl-handler/fidl-handler.h>

#include "keyboard.h"
#include "src/lib/listnode/listnode.h"
#include "vc.h"

static bool keep_log;

static zx_status_t launch_shell(vc_t* vc, int fd, const char* cmd) {
  const char* argv[] = {ZX_SHELL_DEFAULT, nullptr, nullptr, nullptr};

  if (cmd) {
    argv[1] = "-c";
    argv[2] = cmd;
  }

  fdio_spawn_action_t actions[2] = {};
  actions[0].action = FDIO_SPAWN_ACTION_SET_NAME;
  actions[0].name.data = "vc:sh";
  actions[1].action = FDIO_SPAWN_ACTION_TRANSFER_FD;
  actions[1].fd = {.local_fd = fd, .target_fd = FDIO_FLAG_USE_FOR_STDIO};

  uint32_t flags = FDIO_SPAWN_CLONE_ALL & ~FDIO_SPAWN_CLONE_STDIO;

  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_status_t status = fdio_spawn_etc(ZX_HANDLE_INVALID, flags, argv[0], argv, nullptr,
                                      std::size(actions), actions, &vc->proc, err_msg);
  if (status != ZX_OK) {
    printf("vc: cannot spawn shell: %s: %d (%s)\n", err_msg, status, zx_status_get_string(status));
  }
  return status;
}

static void session_destroy(vc_t* vc) {
  if (vc->io != nullptr) {
    fdio_unsafe_release(vc->io);
  }
  if (vc->proc != ZX_HANDLE_INVALID) {
    zx_task_kill(vc->proc);
  }
  // vc_destroy() closes the vc->fd.
  vc_destroy(vc);
}

static void session_io_cb(vc_t* vc, async_dispatcher_t* dispatcher, async::Wait* wait,
                          zx_status_t status, const zx_packet_signal_t* signal) {
  uint32_t pollevt = 0;
  fdio_unsafe_wait_end(vc->io, signal->observed, &pollevt);

  if (pollevt & POLLIN) {
    char data[1024];
    ssize_t r = read(vc->fd, data, sizeof(data));
    if (r > 0) {
      vc_write(vc, data, r, 0);
      wait->Begin(dispatcher);
      return;
    }
  }

  if (pollevt & (POLLRDHUP | POLLHUP)) {
    // shell sessions get restarted on exit
    if (vc->is_shell) {
      zx_task_kill(vc->proc);
      vc->proc = ZX_HANDLE_INVALID;

      int fd = openat(vc->fd, "0", O_RDWR);
      if (fd < 0) {
        session_destroy(vc);
        return;
      }

      if (launch_shell(vc, fd, NULL) < 0) {
        session_destroy(vc);
        return;
      }
      wait->Begin(dispatcher);
      return;
    }
  }
}

static zx_status_t remote_session_create(vc_t** out, zx::channel session, bool make_active,
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
      return status;
    }
    status = fdio_service_connect("/svc/fuchsia.hardware.pty.Device", remote.release());
    if (status != ZX_OK) {
      return status;
    }
    int raw_fd;
    status = fdio_fd_create(local.release(), &raw_fd);
    if (status != ZX_OK) {
      return status;
    }
    fd.reset(raw_fd);
    int flags = fcntl(fd.get(), F_GETFL);
    if (flags < 0) {
      return ZX_ERR_IO;
    }
    if (fcntl(fd.get(), F_SETFL, flags | O_NONBLOCK) < 0) {
      return ZX_ERR_IO;
    }
  }

  fdio_t* io = fdio_unsafe_fd_to_io(fd.get());
  if (io == nullptr) {
    return ZX_ERR_INTERNAL;
  }

  zx_status_t status;
  zx_status_t fidl_status = fuchsia_hardware_pty_DeviceOpenClient(fdio_unsafe_borrow_channel(io), 0,
                                                                  session.release(), &status);
  fdio_unsafe_release(io);
  if (fidl_status != ZX_OK) {
    return fidl_status;
  }
  if (status != ZX_OK) {
    return status;
  }

  vc_t* vc;
  if (vc_create(&vc, color_scheme)) {
    return ZX_ERR_INTERNAL;
  }

  zx_handle_t handle;
  zx_signals_t signals;
  fdio_unsafe_wait_begin(io, POLLIN | POLLRDHUP | POLLHUP, &handle, &signals);
  vc->pty_wait =
      std::make_unique<async::Wait>(handle, signals, 0,
                                    [vc](async_dispatcher_t* dispatcher, async::Wait* wait,
                                         zx_status_t status, const zx_packet_signal_t* signal) {
                                      session_io_cb(vc, dispatcher, wait, status, signal);
                                    });

  fuchsia_hardware_pty_WindowSize wsz = {
      .width = vc->columns,
      .height = vc->rows,
  };

  vc->io = fdio_unsafe_fd_to_io(fd.get());
  fuchsia_hardware_pty_DeviceSetWindowSize(fdio_unsafe_borrow_channel(vc->io), &wsz, &status);

  vc->fd = fd.release();
  if (make_active) {
    vc_set_active(-1, vc);
  }

  *out = vc;
  return ZX_OK;
}

static zx_status_t session_create(vc_t** out, int* out_fd, bool make_active,
                                  const color_scheme_t* color_scheme) {
  zx::channel device_channel, client_channel;
  zx_status_t status = zx::channel::create(0, &device_channel, &client_channel);
  if (status != ZX_OK) {
    return status;
  }

  status = remote_session_create(out, std::move(device_channel), make_active, color_scheme);
  if (status != ZX_OK) {
    return status;
  }

  int raw_client_fd;
  status = fdio_fd_create(client_channel.release(), &raw_client_fd);
  if (status != ZX_OK) {
    return status;
  }
  fbl::unique_fd client_fd(raw_client_fd);

  *out_fd = client_fd.release();
  return ZX_OK;
}

static void start_shell(async_dispatcher_t* dispatcher, bool make_active, const char* cmd,
                        const color_scheme_t* color_scheme) {
  vc_t* vc = nullptr;
  int fd = 0;

  if (session_create(&vc, &fd, make_active, color_scheme) < 0) {
    return;
  }

  vc->is_shell = true;

  if (launch_shell(vc, fd, cmd) < 0) {
    session_destroy(vc);
  } else {
    vc->pty_wait->Begin(dispatcher);
  }
}

static zx_status_t new_vc_cb(void* void_dispatcher, zx_handle_t session, fidl_txn_t* txn) {
  async_dispatcher_t* dispatcher = static_cast<async_dispatcher_t*>(void_dispatcher);
  zx::channel session_channel(session);

  bool make_active = !(keep_log && g_active_vc && g_active_vc == g_log_vc && g_active_vc);
  vc_t* vc = nullptr;
  if (remote_session_create(&vc, std::move(session_channel), make_active,
                            &color_schemes[kDefaultColorScheme]) < 0) {
    return ZX_OK;
  }

  vc->pty_wait->Begin(dispatcher);

  return fuchsia_virtualconsole_SessionManagerCreateSession_reply(txn, ZX_OK);
}

static zx_status_t has_primary_cb(void*, fidl_txn_t* txn) {
  return fuchsia_virtualconsole_SessionManagerHasPrimaryConnected_reply(txn, is_primary_bound());
}

static void fidl_message_cb(async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                            const zx_packet_signal_t* signal) {
  if ((signal->observed & ZX_CHANNEL_PEER_CLOSED) && !(signal->observed & ZX_CHANNEL_READABLE)) {
    zx_handle_close(wait->object());
    delete wait;
    return;
  }

  status = fs::ReadMessage(wait->object(),
                           [dispatcher](fidl_incoming_msg_t* message, fs::FidlConnection* txn) {
                             static constexpr fuchsia_virtualconsole_SessionManager_ops_t kOps{
                                 .CreateSession = new_vc_cb,
                                 .HasPrimaryConnected = has_primary_cb,
                             };

                             return fuchsia_virtualconsole_SessionManager_dispatch(
                                 dispatcher, reinterpret_cast<fidl_txn_t*>(txn), message, &kOps);
                           });

  if (status != ZX_OK) {
    printf("Failed to dispatch fidl message from client: %s\n", zx_status_get_string(status));
    zx_handle_close(wait->object());
    delete wait;
    return;
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    printf("Failed to reinitialize fidl_message_cb: %s\n", zx_status_get_string(status));
    zx_handle_close(wait->object());
    delete wait;
    return;
  }
}

static void fidl_connection_cb(async_dispatcher_t* dispatcher, async::Wait* wait,
                               zx_status_t status, const zx_packet_signal_t* signal) {
  constexpr size_t kBufferSize = 256;
  char buffer[kBufferSize];

  uint32_t bytes_read, handles_read;
  zx_handle_t client_raw;
  status = zx_channel_read(wait->object(), 0, buffer, &client_raw, kBufferSize, 1, &bytes_read,
                           &handles_read);
  if (status != ZX_OK) {
    printf("Failed to read from channel: %s\n", zx_status_get_string(status));
    return;
  }

  if (handles_read < 1) {
    printf("Fidl connection with no channel.\n");
    return;
  }
  zx::channel client(client_raw);

  if (fbl::StringPiece(fuchsia_virtualconsole_SessionManager_Name) ==
      fbl::StringPiece(buffer, bytes_read)) {
    // This fidl_wait is freed on error in fidl_message_cb.
    async::Wait* fidl_wait = new async::Wait(
        client.release(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, 0, fidl_message_cb);
    status = fidl_wait->Begin(dispatcher);
    if (status != ZX_OK) {
      printf("Error starting fidl_wait: %d\n", status);
    }
  } else {
    printf("Unsupported fidl interface: %.*s\n", bytes_read, buffer);
  }
  wait->Begin(dispatcher);
}

int main(int argc, char** argv) {
  // NOTE: devmgr has getenv_bool. when more options are added, consider
  // sharing that.
  const char* value = getenv("virtcon.keep-log-visible");
  if (value == NULL ||
      ((strcmp(value, "0") == 0) || (strcmp(value, "false") == 0) || (strcmp(value, "off") == 0))) {
    keep_log = false;
  } else {
    keep_log = true;
  }

  const char* cmd = NULL;
  int shells = 0;
  while (argc > 1) {
    if (!strcmp(argv[1], "--run")) {
      if (argc > 2) {
        argc--;
        argv++;
        cmd = argv[1];
        if (shells < 1)
          shells = 1;
        printf("CMD: %s\n", cmd);
      }
    } else if (!strcmp(argv[1], "--shells")) {
      if (argc > 2) {
        argc--;
        argv++;
        shells = atoi(argv[1]);
      }
    }
    argc--;
    argv++;
  }

  char* colorvar = getenv("virtcon.colorscheme");
  const color_scheme_t* color_scheme = string_to_color_scheme(colorvar);
  if (cmd != NULL) {
    color_scheme = &color_schemes[kSpecialColorScheme];
  }

  async::Loop loop = async::Loop(&kAsyncLoopConfigNeverAttachToThread);

  if (log_start(loop.dispatcher()) < 0) {
    return -1;
  }

  async::Wait new_vc_wait;
  {
    zx_handle_t startup_handle = zx_take_startup_handle(PA_HND(PA_USER0, 0));
    if (startup_handle != ZX_HANDLE_INVALID) {
      new_vc_wait.set_object(startup_handle);
      new_vc_wait.set_trigger(ZX_CHANNEL_READABLE);
      new_vc_wait.set_handler(fidl_connection_cb);
      new_vc_wait.Begin(loop.dispatcher());
    }
  }

  bool repeat_keys = true;
  {
    char* flag = getenv("virtcon.keyrepeat");
    if (flag && (!strcmp(flag, "0") || !strcmp(flag, "false"))) {
      printf("vc: Key repeat disabled\n");
      repeat_keys = false;
    }
  }

  zx_status_t status = setup_keyboard_watcher(loop.dispatcher(), handle_key_press, repeat_keys);
  if (status != ZX_OK) {
    printf("vc: setup_keyboard_watcher failed with %d\n", status);
  }

  if (!vc_sysmem_connect()) {
    return -1;
  }

  if (!vc_display_init(loop.dispatcher())) {
    return -1;
  }

  setenv("TERM", "xterm", 1);

  for (int i = 0; i < shells; ++i) {
    if (i == 0)
      start_shell(loop.dispatcher(), !keep_log, cmd, color_scheme);
    else
      start_shell(loop.dispatcher(), false, NULL, color_scheme);
  }

  status = loop.Run();
  printf("vc: loop stopped: %d\n", status);
  return -1;
}
