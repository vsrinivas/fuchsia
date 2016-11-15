// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/moterm/command.h"

#include <launchpad/launchpad.h>
#include <magenta/device/device.h>
#include <magenta/device/input.h>
#include <magenta/processargs.h>
#include <magenta/types.h>
#include <mxio/io.h>
#include <mxio/util.h>
#include <poll.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

namespace moterm {

namespace {
mx::socket CreateRemotePipe(uint32_t destination,
                            mx_handle_t* out_handle,
                            uint32_t* out_id) {
  mx_handle_t socket_handle;
  uint32_t socket_id;
  mx_status_t status = mxio_pipe_half(&socket_handle, &socket_id);
  if (status < 0) {
    return mx::socket();
  }
  mx::socket socket(socket_handle);
  int remote_fd = status;
  status = mxio_clone_fd(remote_fd, destination, out_handle, out_id);
  FTL_CHECK(status == 1);
  close(remote_fd);
  return socket;
}
}  // namespace

Command::Command(CommandCallback callback) : callback_(callback) {}

Command::~Command() {
  if (out_key_) {
    mtl::MessageLoop::GetCurrent()->RemoveHandler(out_key_);
  }
  if (err_key_) {
    mtl::MessageLoop::GetCurrent()->RemoveHandler(err_key_);
  }
}

bool Command::Start(
    const char* name,
    int argc,
    const char* const* argv,
    fidl::InterfaceHandle<modular::ApplicationEnvironment> environment) {
  mx_handle_t handles[4];
  uint32_t ids[4];
  uint32_t count = 0;

  stdin_ = CreateRemotePipe(STDIN_FILENO, handles + count, ids + count);
  if (!stdin_) {
    FTL_LOG(ERROR) << "Failed to create stdin pipe";
    return false;
  }
  count++;

  stdout_ = CreateRemotePipe(STDOUT_FILENO, handles + count, ids + count);
  if (!stdout_) {
    FTL_LOG(ERROR) << "Failed to create stdout pipe";
    return false;
  }
  count++;

  stderr_ = CreateRemotePipe(STDERR_FILENO, handles + count, ids + count);
  if (!stderr_) {
    FTL_LOG(ERROR) << "Failed to create stderr pipe";
    return false;
  }
  count++;

  if (environment) {
    handles[count] =
        static_cast<mx_handle_t>(environment.PassHandle().release()),
    ids[count] = MX_HND_TYPE_APPLICATION_ENVIRONMENT;
    count++;
  }

  mx_handle_t result =
      launchpad_launch_mxio_etc(name, argc, argv, nullptr, count, handles, ids);
  if (result < 0) {
    FTL_LOG(ERROR) << "Failed to start process";
    return false;
  }
  process_.reset(result);

  mx_signals_t signals = MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED;

  out_key_ = mtl::MessageLoop::GetCurrent()->AddHandler(
      this, stdout_.get(), signals, ftl::TimeDelta::Max());
  if (!out_key_) {
    FTL_LOG(ERROR) << "Failed to monitor stdout";
    return false;
  }
  err_key_ = mtl::MessageLoop::GetCurrent()->AddHandler(
      this, stderr_.get(), signals, ftl::TimeDelta::Max());
  if (!err_key_) {
    FTL_LOG(ERROR) << "Failed to monitor sterr";
    return false;
  }

  FTL_LOG(INFO) << "Command " << name << " started";
  return true;
}

void Command::OnHandleReady(mx_handle_t handle, mx_signals_t pending) {
  if (pending & MX_SIGNAL_READABLE) {
    char buffer[2048];
    mx_size_t len;

    if (handle == stdout_.get()) {
      if (stdout_.read(0, buffer, 2048, &len) != NO_ERROR) {
        return;
      }
    } else if (handle == stderr_.get()) {
      if (stderr_.read(0, buffer, 2048, &len) != NO_ERROR) {
        return;
      }
    } else {
      return;
    }
    buffer[len] = '\0';

    callback_(buffer, len);
  } else if (pending & MX_SIGNAL_PEER_CLOSED) {
    FTL_LOG(INFO) << "Command exited";
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  }
}

void Command::SendData(const void* bytes, size_t num_bytes) {
  mx_size_t len;
  if (stdin_.write(0, bytes, num_bytes, &len) != NO_ERROR) {
    FTL_LOG(ERROR) << "Failed to send";
  }
}

}  // namespace moterm
