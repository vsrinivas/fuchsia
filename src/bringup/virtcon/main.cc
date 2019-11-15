// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <fuchsia/hardware/pty/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <fuchsia/virtualconsole/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/channel.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/device/vfs.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <zircon/syscalls/object.h>

#include <memory>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string_piece.h>
#include <fbl/unique_fd.h>
#include <fs/handler.h>

#include "vc.h"

port_t port;
static port_handler_t new_vc_ph;
static port_handler_t input_ph;

static int input_dir_fd;

static zx_status_t launch_shell(vc_t* vc, int fd, const char* cmd) {
  const char* argv[] = {"/boot/bin/sh", nullptr, nullptr, nullptr};

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
                                      fbl::count_of(actions), actions, &vc->proc, err_msg);
  if (status != ZX_OK) {
    printf("vc: cannot spawn shell: %s: %d (%s)\n", err_msg, status, zx_status_get_string(status));
  }
  return status;
}

static void session_destroy(vc_t* vc) {
  if (vc->fd >= 0) {
    port_fd_handler_done(&vc->fh);
    // vc_destroy() closes the fd
  }
  if (vc->proc != ZX_HANDLE_INVALID) {
    zx_task_kill(vc->proc);
  }
  vc_destroy(vc);
}

static zx_status_t session_io_cb(port_fd_handler_t* fh, unsigned pollevt, uint32_t evt) {
  vc_t* vc = containerof(fh, vc_t, fh);

  if (pollevt & POLLIN) {
    char data[1024];
    ssize_t r = read(vc->fd, data, sizeof(data));
    if (r > 0) {
      vc_write(vc, data, r, 0);
      return ZX_OK;
    }
  }

  if (pollevt & (POLLRDHUP | POLLHUP)) {
    // shell sessions get restarted on exit
    if (vc->is_shell) {
      zx_task_kill(vc->proc);
      vc->proc = ZX_HANDLE_INVALID;

      int fd = openat(vc->fd, "0", O_RDWR);
      if (fd < 0) {
        goto fail;
      }

      if (launch_shell(vc, fd, NULL) < 0) {
        goto fail;
      }
      return ZX_OK;
    }
  }

fail:
  session_destroy(vc);
  return ZX_ERR_STOP;
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
  zx_status_t r;
  if ((r = port_fd_handler_init(&vc->fh, fd.get(), POLLIN | POLLRDHUP | POLLHUP)) < 0) {
    vc_destroy(vc);
    return r;
  }

  fuchsia_hardware_pty_WindowSize wsz = {
      .width = vc->columns,
      .height = vc->rows,
  };

  io = fdio_unsafe_fd_to_io(fd.get());
  fuchsia_hardware_pty_DeviceSetWindowSize(fdio_unsafe_borrow_channel(io), &wsz, &status);
  fdio_unsafe_release(io);

  vc->fd = fd.release();
  if (make_active) {
    vc_set_active(-1, vc);
  }

  vc->fh.func = session_io_cb;

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

static void start_shell(bool make_active, const char* cmd, const color_scheme_t* color_scheme) {
  vc_t* vc = nullptr;
  int fd = 0;

  if (session_create(&vc, &fd, make_active, color_scheme) < 0) {
    return;
  }

  vc->is_shell = true;

  if (launch_shell(vc, fd, cmd) < 0) {
    session_destroy(vc);
  } else {
    port_wait(&port, &vc->fh.ph);
  }
}

static zx_status_t new_vc_cb(void*, zx_handle_t session, fidl_txn_t* txn) {
  zx::channel session_channel(session);

  vc_t* vc = nullptr;
  if (remote_session_create(&vc, std::move(session_channel), true,
                            &color_schemes[kDefaultColorScheme]) < 0) {
    return ZX_OK;
  }

  port_wait(&port, &vc->fh.ph);

  return fuchsia_virtualconsole_SessionManagerCreateSession_reply(txn, ZX_OK);
}

static zx_status_t has_primary_cb(void*, fidl_txn_t* txn) {
  return fuchsia_virtualconsole_SessionManagerHasPrimaryConnected_reply(txn, is_primary_bound());
}

static zx_status_t fidl_message_cb(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
  if ((signals & ZX_CHANNEL_PEER_CLOSED) && !(signals & ZX_CHANNEL_READABLE)) {
    zx_handle_close(ph->handle);
    delete ph;
    return ZX_ERR_STOP;
  }

  auto status = fs::ReadMessage(ph->handle, [](fidl_msg_t* message, fs::FidlConnection* txn) {
    static constexpr fuchsia_virtualconsole_SessionManager_ops_t kOps{
        .CreateSession = new_vc_cb,
        .HasPrimaryConnected = has_primary_cb,
    };

    return fuchsia_virtualconsole_SessionManager_dispatch(
        nullptr, reinterpret_cast<fidl_txn_t*>(txn), message, &kOps);
  });

  if (status != ZX_OK) {
    printf("Failed to dispatch fidl message from client: %s\n", zx_status_get_string(status));
    zx_handle_close(ph->handle);
    delete ph;
    return ZX_ERR_STOP;
  }

  return ZX_OK;
}

static zx_status_t fidl_connection_cb(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
  constexpr size_t kBufferSize = 256;
  char buffer[kBufferSize];

  uint32_t bytes_read, handles_read;
  zx_handle_t client_raw;
  auto status = zx_channel_read(ph->handle, 0, buffer, &client_raw, kBufferSize, 1, &bytes_read,
                                &handles_read);
  if (status != ZX_OK) {
    printf("Failed to read from channel: %s\n", zx_status_get_string(status));
    return ZX_OK;
  }

  if (handles_read < 1) {
    printf("Fidl connection with no channel.\n");
    return ZX_OK;
  }
  zx::channel client(client_raw);

  if (fbl::StringPiece(fuchsia_virtualconsole_SessionManager_Name) ==
      fbl::StringPiece(buffer, bytes_read)) {
    auto handler = std::unique_ptr<port_handler_t>(new port_handler_t{
        .handle = client.release(),
        .waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
        .func = fidl_message_cb,
    });

    port_wait(&port, handler.release());
  } else {
    printf("Unsupported fidl interface: %.*s\n", bytes_read, buffer);
  }

  return ZX_OK;
}

static zx_status_t input_dir_event(unsigned evt, const char* name) {
  if ((evt != fuchsia_io_WATCH_EVENT_EXISTING) && (evt != fuchsia_io_WATCH_EVENT_ADDED)) {
    return ZX_OK;
  }

  printf("vc: new input device /dev/class/input/%s\n", name);

  int fd;
  if ((fd = openat(input_dir_fd, name, O_RDONLY)) < 0) {
    return ZX_OK;
  }

  new_input_device(fd, handle_key_press);
  return ZX_OK;
}

static void setup_dir_watcher(const char* dir,
                              zx_status_t (*cb)(port_handler_t*, zx_signals_t, uint32_t),
                              port_handler_t* ph, int* fd_out) {
  *fd_out = -1;
  fbl::unique_fd fd(open(dir, O_DIRECTORY | O_RDONLY));
  if (!fd) {
    return;
  }
  zx::channel client, server;
  if (zx::channel::create(0, &client, &server) != ZX_OK) {
    return;
  }

  fzl::FdioCaller caller(std::move(fd));
  zx_status_t status;
  zx_status_t io_status = fuchsia_io_DirectoryWatch(
      caller.borrow_channel(), fuchsia_io_WATCH_MASK_ALL, 0, server.release(), &status);
  if (io_status != ZX_OK || status != ZX_OK) {
    return;
  }

  *fd_out = caller.release().release();
  ph->handle = client.release();
  ph->waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
  ph->func = cb;
  port_wait(&port, ph);
}

zx_status_t handle_device_dir_event(port_handler_t* ph, zx_signals_t signals,
                                    zx_status_t (*event_handler)(unsigned event, const char* msg)) {
  if (!(signals & ZX_CHANNEL_READABLE)) {
    printf("vc: device directory died\n");
    return ZX_ERR_STOP;
  }

  // Buffer contains events { Opcode, Len, Name[Len] }
  // See zircon/device/vfs.h for more detail
  // extra byte is for temporary NUL
  uint8_t buf[fuchsia_io_MAX_BUF + 1];
  uint32_t len;
  if (zx_channel_read(ph->handle, 0, buf, NULL, sizeof(buf) - 1, 0, &len, NULL) < 0) {
    printf("vc: failed to read from device directory\n");
    return ZX_ERR_STOP;
  }

  uint8_t* msg = buf;
  while (len >= 2) {
    uint8_t event = *msg++;
    uint8_t namelen = *msg++;
    if (len < (namelen + 2u)) {
      printf("vc: malformed device directory message\n");
      return ZX_ERR_STOP;
    }
    // add temporary nul
    uint8_t tmp = msg[namelen];
    msg[namelen] = 0;
    zx_status_t status = event_handler(event, (char*)msg);
    if (status != ZX_OK) {
      return status;
    }
    msg[namelen] = tmp;
    msg += namelen;
    len -= (namelen + 2u);
  }
  return ZX_OK;
}

static zx_status_t input_cb(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
  return handle_device_dir_event(ph, signals, input_dir_event);
}

int main(int argc, char** argv) {
  // NOTE: devmgr has getenv_bool. when more options are added, consider
  // sharing that.
  bool keep_log = false;
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

  if (port_init(&port) < 0) {
    return -1;
  }

  if (log_start() < 0) {
    return -1;
  }

  if ((new_vc_ph.handle = zx_take_startup_handle(PA_HND(PA_USER0, 0))) != ZX_HANDLE_INVALID) {
    new_vc_ph.func = fidl_connection_cb;
    new_vc_ph.waitfor = ZX_CHANNEL_READABLE;
    port_wait(&port, &new_vc_ph);
  }

  setup_dir_watcher("/dev/class/input", input_cb, &input_ph, &input_dir_fd);

  if (!vc_sysmem_connect()) {
    return -1;
  }

  if (!vc_display_init()) {
    return -1;
  }

  setenv("TERM", "xterm", 1);

  for (int i = 0; i < shells; ++i) {
    if (i == 0)
      start_shell(!keep_log, cmd, color_scheme);
    else
      start_shell(false, NULL, color_scheme);
  }

  zx_status_t r = port_dispatch(&port, ZX_TIME_INFINITE, false);
  printf("vc: port failure: %d\n", r);
  return -1;
}
