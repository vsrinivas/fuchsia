// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest/vshc.h"

#include <fcntl.h>
#include <fuchsia/hardware/pty/c/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <poll.h>
#include <unistd.h>
#include <zircon/status.h>

#include <algorithm>
#include <iostream>

#include <google/protobuf/message_lite.h>

#include "src/lib/fsl/socket/socket_drainer.h"
#include "src/lib/fsl/tasks/fd_waiter.h"
#include "src/lib/fxl/logging.h"
#include "src/virtualization/lib/vsh/util.h"
#include "src/virtualization/packages/biscotti_guest/third_party/protos/vsh.pb.h"

std::pair<int, int> init_tty(void) {
  int cols = 80;
  int rows = 24;

  if (isatty(STDIN_FILENO)) {
    fdio_t* io = fdio_unsafe_fd_to_io(STDIN_FILENO);
    fuchsia_hardware_pty_WindowSize wsz;
    zx_status_t status;
    zx_status_t call_status =
        fuchsia_hardware_pty_DeviceGetWindowSize(fdio_unsafe_borrow_channel(io), &status, &wsz);

    if (call_status != ZX_OK || status != ZX_OK) {
      FXL_LOG(WARNING) << "Unable to determine shell geometry, defaulting to 80x24";
    } else {
      cols = wsz.width;
      rows = wsz.height;
    }

    // Enable raw mode on tty so that inputs such as ctrl-c are passed on
    // faithfully to the client for forwarding to the remote shell
    // (instead of closing the client side).
    uint32_t feature;
    call_status = fuchsia_hardware_pty_DeviceClrSetFeature(
        fdio_unsafe_borrow_channel(io), 0, fuchsia_hardware_pty_FEATURE_RAW, &status, &feature);

    if (call_status != ZX_OK || status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to set FEATURE_RAW, some features may not work.";
    }

    fdio_unsafe_release(io);
  }

  return {cols, rows};
}

void reset_tty(void) {
  if (isatty(STDIN_FILENO)) {
    fdio_t* io = fdio_unsafe_fd_to_io(STDIN_FILENO);
    uint32_t feature;
    zx_status_t status;
    zx_status_t call_status = fuchsia_hardware_pty_DeviceClrSetFeature(
        fdio_unsafe_borrow_channel(io), fuchsia_hardware_pty_FEATURE_RAW, 0, &status, &feature);

    if (call_status != ZX_OK || status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to reset FEATURE_RAW";
    }

    fdio_unsafe_release(io);
  }
}

namespace {
class ConsoleIn {
 public:
  ConsoleIn(async::Loop* loop, zx::unowned_socket&& socket)
      : loop_(loop), sink_(std::move(socket)), fd_waiter_(loop->dispatcher()) {}

  bool Start(void) {
    if (fcntl(STDIN_FILENO, F_GETFD) != -1) {
      fd_waiter_.Wait(fit::bind_member(this, &ConsoleIn::HandleStdin), STDIN_FILENO, POLLIN);
    } else {
      FXL_LOG(ERROR) << "Unable to start the async output loop";
      return false;
    }

    return true;
  }

  void HandleStdin(zx_status_t status, uint32_t events) {
    if (status != ZX_OK && status != ZX_ERR_SHOULD_WAIT) {
      loop_->Shutdown();
      return;
    }

    vm_tools::vsh::GuestMessage msg_out;
    uint8_t buf[vsh::kMaxDataSize];
    ssize_t actual = read(STDIN_FILENO, buf, vsh::kMaxDataSize);

    msg_out.mutable_data_message()->set_stream(vm_tools::vsh::STDIN_STREAM);
    msg_out.mutable_data_message()->set_data(buf, actual);
    if (!vsh::SendMessage(*sink_, msg_out)) {
      FXL_LOG(ERROR) << "Failed to send stdin";
      return;
    }

    fd_waiter_.Wait(fit::bind_member(this, &ConsoleIn::HandleStdin), STDIN_FILENO, POLLIN);
  }

 private:
  async::Loop* loop_;
  zx::unowned_socket sink_;
  fsl::FDWaiter fd_waiter_;
};

class ConsoleOut {
 public:
  ConsoleOut(async::Loop* loop, zx::unowned_socket&& socket)
      : loop_(loop),
        source_(std::move(socket)),
        reading_size_(true),
        msg_size_(sizeof(uint32_t)),
        bytes_left_(msg_size_) {}

  bool Start(void) {
    wait_.set_object(source_->get());
    wait_.set_trigger(ZX_SOCKET_READABLE);
    auto status = wait_.Begin(loop_->dispatcher());
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Unable to start the async input loop";
      return false;
    }

    return true;
  }

  void HandleTtyOutput(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) {
    if (status != ZX_OK && status != ZX_ERR_SHOULD_WAIT) {
      loop_->Shutdown();
      loop_->Quit();
      return;
    }

    vm_tools::vsh::HostMessage msg_in;

    if (status != ZX_ERR_SHOULD_WAIT && bytes_left_) {
      size_t actual;
      source_->read(0, buf_ + (msg_size_ - bytes_left_), bytes_left_, &actual);
      bytes_left_ -= actual;
    }

    if (bytes_left_ == 0 && reading_size_) {
      uint32_t sz;
      std::memcpy(&sz, buf_, sizeof(sz));

      // set state for next iteration
      reading_size_ = false;
      msg_size_ = le32toh(sz);
      bytes_left_ = msg_size_;

      FXL_CHECK(msg_size_ <= vsh::kMaxMessageSize)
          << "Message size of " << msg_size_ << " exceeds kMaxMessageSize";

    } else if (bytes_left_ == 0 && !reading_size_) {
      FXL_CHECK(msg_in.ParseFromArray(buf_, msg_size_)) << "Failed to parse incoming message.";

      reading_size_ = true;
      msg_size_ = sizeof(uint32_t);
      bytes_left_ = msg_size_;

      switch (msg_in.msg_case()) {
        case vm_tools::vsh::HostMessage::MsgCase::kDataMessage: {
          auto data = msg_in.data_message().data();
          write(STDOUT_FILENO, data.data(), data.size());
        } break;
        case vm_tools::vsh::HostMessage::MsgCase::kStatusMessage:
          if (msg_in.status_message().status() != vm_tools::vsh::READY) {
            loop_->Shutdown();
            loop_->Quit();
            reset_tty();
            if (msg_in.status_message().status() == vm_tools::vsh::EXITED) {
              exit(msg_in.status_message().code());
            } else {
              FXL_LOG(ERROR) << "vsh did not complete successfully.";
              exit(-1);
            }
          }
          break;
        default:
          FXL_LOG(WARNING) << "Unhandled HostMessage received.";
      }
    }

    status = wait->Begin(dispatcher);
  }

 private:
  async::WaitMethod<ConsoleOut, &ConsoleOut::HandleTtyOutput> wait_{this};
  async::Loop* loop_;
  zx::unowned_socket source_;

  uint8_t buf_[vsh::kMaxMessageSize];
  bool reading_size_;
  size_t msg_size_;
  size_t bytes_left_;
};

}  // namespace

static bool init_shell(const zx::socket& usock) {
  vm_tools::vsh::SetupConnectionRequest conn_req;
  vm_tools::vsh::SetupConnectionResponse conn_resp;

  // Target can be |vsh::kVmShell| or the empty string for the VM.
  // Specifying container name directly here is not supported.
  conn_req.set_target("");
  // User can be defaulted with empty string. This is chronos for vmshell and
  // root otherwise
  conn_req.set_user("");
  // Blank command for login shell. (other uses deprecated, use argv directly
  // instead)
  conn_req.set_command("");
  conn_req.clear_argv();

  auto env = conn_req.mutable_env();
  if (auto term_env = getenv("TERM"))
    (*env)["TERM"] = std::string(term_env);

  (*env)["LXD_DIR"] = "/mnt/stateful/lxd";
  (*env)["LXD_CONF"] = "/mnt/stateful/lxd_conf";
  (*env)["LXD_UNPRIVILEGED_ONLY"] = "true";

  if (!vsh::SendMessage(usock, conn_req)) {
    FXL_LOG(ERROR) << "Failed to send connection request";
    return false;
  }

  // No use setting up the async message handling if we haven't even
  // connected properly. Block on connection response.
  if (!vsh::RecvMessage(usock, &conn_resp)) {
    FXL_LOG(ERROR) << "Failed to receive response from vshd, giving up after one try";
    return false;
  }

  if (conn_resp.status() != vm_tools::vsh::READY) {
    FXL_LOG(ERROR) << "Server was unable to set up connection properly: "
                   << conn_resp.description();
    return false;
  }

  // Connection to server established.
  // Initial Configuration Phase.
  auto [cols, rows] = init_tty();
  vm_tools::vsh::GuestMessage msg_out;
  msg_out.mutable_resize_message()->set_cols(cols);
  msg_out.mutable_resize_message()->set_rows(rows);
  if (!vsh::SendMessage(usock, msg_out)) {
    FXL_LOG(ERROR) << "Failed to send window resize message";
    return false;
  }

  return true;
}

void handle_vsh(std::optional<uint32_t> o_env_id, std::optional<uint32_t> o_cid,
                std::optional<uint32_t> o_port, async::Loop* loop, sys::ComponentContext* context) {
  uint32_t env_id, cid, port = o_port.value_or(vsh::kVshPort);

  fuchsia::virtualization::ManagerSyncPtr manager;
  context->svc()->Connect(manager.NewRequest());
  std::vector<fuchsia::virtualization::EnvironmentInfo> env_infos;
  manager->List(&env_infos);
  if (env_infos.size() == 0) {
    FXL_LOG(ERROR) << "Unable to find any environments.";
    return;
  }
  env_id = o_env_id.value_or(env_infos[0].id);

  fuchsia::virtualization::RealmSyncPtr realm;
  manager->Connect(env_id, realm.NewRequest());
  std::vector<fuchsia::virtualization::InstanceInfo> instances;
  realm->ListInstances(&instances);
  if (instances.size() == 0) {
    FXL_LOG(ERROR) << "Unable to find any instances in environment " << env_id;
    return;
  }
  cid = o_cid.value_or(instances[0].cid);

  // Verify the environment and instance specified exist
  if (std::find_if(env_infos.begin(), env_infos.end(),
                   [env_id](auto ei) { return ei.id == env_id; }) == env_infos.end()) {
    FXL_LOG(ERROR) << "No existing environment with id " << env_id;
    return;
  }
  if (std::find_if(instances.begin(), instances.end(), [cid](auto in) { return in.cid == cid; }) ==
      instances.end()) {
    FXL_LOG(ERROR) << "No existing instances in env " << env_id << " with cid " << cid;
    return;
  }

  fuchsia::virtualization::HostVsockEndpointSyncPtr vsock_endpoint;
  realm->GetHostVsockEndpoint(vsock_endpoint.NewRequest());

  // Open a socket to the guest's vsock port where vshd should be listening
  zx::socket socket, remote_socket;
  zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create socket: " << zx_status_get_string(status);
    return;
  }
  vsock_endpoint->Connect(cid, port, std::move(remote_socket), &status);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to connect: " << zx_status_get_string(status);
    return;
  }

  // Now |socket| is a zircon socket plumbed to a port on the guest's vsock
  // interface. The vshd service is hopefully on the other end of this pipe.
  // We communicate with the service via protobuf messages.
  if (!init_shell(socket)) {
    FXL_LOG(ERROR) << "vsh SetupConnection failed.";
    return;
  }

  // Directly inject some helper functions for connecting to container.
  // This sleep below is to give bash some time to start after being `exec`d.
  // Otherwise the input will be duplicated in the output stream.
  usleep(100'000);
  vm_tools::vsh::GuestMessage msg_out;
  msg_out.mutable_data_message()->set_stream(vm_tools::vsh::STDIN_STREAM);
  msg_out.mutable_data_message()->set_data(
      "function stretch() { lxc exec stretch -- login -f machina ; } \n\n");
  if (!vsh::SendMessage(socket, msg_out)) {
    FXL_LOG(WARNING) << "Failed to inject helper function";
  }

  // Set up the I/O loops
  ConsoleIn i(loop, zx::unowned_socket(socket));
  ConsoleOut o(loop, zx::unowned_socket(socket));

  bool success = true;
  if (!i.Start()) {
    FXL_LOG(ERROR) << "Problem starting ConsoleIn loop";
    success = false;
  }
  if (!o.Start()) {
    FXL_LOG(ERROR) << "Problem starting ConsoleOut loop";
    success = false;
  }

  if (success) {
    loop->Run();
  }

  reset_tty();
}
