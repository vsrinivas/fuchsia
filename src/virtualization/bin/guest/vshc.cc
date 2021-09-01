// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest/vshc.h"

#include <fcntl.h>
#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <poll.h>
#include <unistd.h>
#include <zircon/status.h>

#include <algorithm>
#include <iostream>

#include <google/protobuf/message_lite.h>

#include "src/lib/fsl/socket/socket_drainer.h"
#include "src/lib/fsl/tasks/fd_waiter.h"
#include "src/virtualization/bin/guest/services.h"
#include "src/virtualization/lib/vsh/util.h"
#include "src/virtualization/third_party/vm_tools/vsh.pb.h"

namespace fpty = fuchsia_hardware_pty;

std::pair<int, int> init_tty() {
  int cols = 80;
  int rows = 24;

  if (isatty(STDIN_FILENO)) {
    fdio_t* io = fdio_unsafe_fd_to_io(STDIN_FILENO);
    auto wsz = fidl::WireCall<fpty::Device>(zx::unowned_channel(fdio_unsafe_borrow_channel(io)))
                   .GetWindowSize();

    if (wsz.status() != ZX_OK || wsz->status != ZX_OK) {
      std::cerr << "Warning: Unable to determine shell geometry, defaulting to 80x24.\n";
    } else {
      cols = wsz->size.width;
      rows = wsz->size.height;
    }

    // Enable raw mode on tty so that inputs such as ctrl-c are passed on
    // faithfully to the client for forwarding to the remote shell
    // (instead of closing the client side).
    auto result = fidl::WireCall<fpty::Device>(zx::unowned_channel(fdio_unsafe_borrow_channel(io)))
                      .ClrSetFeature(0, fpty::wire::kFeatureRaw);

    if (result.status() != ZX_OK || result->status != ZX_OK) {
      std::cerr << "Warning: Failed to set FEATURE_RAW, some features may not work.\n";
    }

    fdio_unsafe_release(io);
  }

  return {cols, rows};
}

void reset_tty() {
  if (isatty(STDIN_FILENO)) {
    fdio_t* io = fdio_unsafe_fd_to_io(STDIN_FILENO);
    auto result = fidl::WireCall<fpty::Device>(zx::unowned_channel(fdio_unsafe_borrow_channel(io)))
                      .ClrSetFeature(fpty::wire::kFeatureRaw, 0);

    if (result.status() != ZX_OK || result->status != ZX_OK) {
      std::cerr << "Failed to reset FEATURE_RAW.\n";
    }

    fdio_unsafe_release(io);
  }
}

namespace {
class ConsoleIn {
 public:
  ConsoleIn(async::Loop* loop, zx::unowned_socket&& socket)
      : loop_(loop), sink_(std::move(socket)), fd_waiter_(loop->dispatcher()) {}

  bool Start() {
    if (fcntl(STDIN_FILENO, F_GETFD) != -1) {
      fd_waiter_.Wait(fit::bind_member(this, &ConsoleIn::HandleStdin), STDIN_FILENO, POLLIN);
    } else {
      std::cerr << "Unable to start the async output loop.\n";
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
      std::cerr << "Failed to send stdin.\n";
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

  bool Start() {
    wait_.set_object(source_->get());
    wait_.set_trigger(ZX_SOCKET_READABLE);
    auto status = wait_.Begin(loop_->dispatcher());
    if (status != ZX_OK) {
      std::cerr << "Unable to start the async input loop.\n";
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

      FX_CHECK(msg_size_ <= vsh::kMaxMessageSize)
          << "Message size of " << msg_size_ << " exceeds kMaxMessageSize";

    } else if (bytes_left_ == 0 && !reading_size_) {
      FX_CHECK(msg_in.ParseFromArray(buf_, msg_size_)) << "Failed to parse incoming message.";

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
              std::cerr << "vsh did not complete successfully.\n";
              exit(-1);
            }
          }
          break;
        default:
          std::cerr << "Unhandled HostMessage received.\n";
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
  uint32_t msg_size_;
  uint32_t bytes_left_;
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
    std::cerr << "Failed to send connection request.\n";
    return false;
  }

  // No use setting up the async message handling if we haven't even
  // connected properly. Block on connection response.
  if (!vsh::RecvMessage(usock, &conn_resp)) {
    std::cerr << "Failed to receive response from vshd, giving up after one try.\n";
    return false;
  }

  if (conn_resp.status() != vm_tools::vsh::READY) {
    std::cerr << "Server was unable to set up connection properly: " << conn_resp.description()
              << '\n';
    return false;
  }

  // Connection to server established.
  // Initial Configuration Phase.
  auto [cols, rows] = init_tty();
  vm_tools::vsh::GuestMessage msg_out;
  msg_out.mutable_resize_message()->set_cols(cols);
  msg_out.mutable_resize_message()->set_rows(rows);
  if (!vsh::SendMessage(usock, msg_out)) {
    std::cerr << "Failed to send window resize message.\n";
    return false;
  }

  return true;
}

zx_status_t handle_vsh(std::optional<uint32_t> o_env_id, std::optional<uint32_t> o_cid,
                       std::optional<uint32_t> o_port, async::Loop* loop,
                       sys::ComponentContext* context) {
  uint32_t env_id, cid, port = o_port.value_or(vsh::kVshPort);

  // Connect to the manager.
  zx::status<fuchsia::virtualization::ManagerSyncPtr> manager = ConnectToManager(context);
  if (manager.is_error()) {
    return manager.error_value();
  }

  std::vector<fuchsia::virtualization::EnvironmentInfo> env_infos;
  zx_status_t status = manager->List(&env_infos);
  if (status != ZX_OK) {
    std::cerr << "Could not fetch list of environments: " << zx_status_get_string(status) << ".\n";
    return status;
  }
  if (env_infos.empty()) {
    std::cerr << "Unable to find any environments.\n";
    return ZX_ERR_NOT_FOUND;
  }
  env_id = o_env_id.value_or(env_infos[0].id);

  fuchsia::virtualization::RealmSyncPtr realm;
  manager->Connect(env_id, realm.NewRequest());
  std::vector<fuchsia::virtualization::InstanceInfo> instances;
  status = realm->ListInstances(&instances);
  if (status != ZX_OK) {
    std::cerr << "Could not fetch list of instances: " << zx_status_get_string(status) << ".\n";
    return status;
  }
  if (instances.empty()) {
    std::cerr << "Unable to find any instances in environment " << env_id << '\n';
    return ZX_ERR_NOT_FOUND;
  }
  cid = o_cid.value_or(instances[0].cid);

  // Verify the environment and instance specified exist
  if (std::find_if(env_infos.begin(), env_infos.end(),
                   [env_id](auto ei) { return ei.id == env_id; }) == env_infos.end()) {
    std::cerr << "No existing environment with id " << env_id << '\n';
    return ZX_ERR_NOT_FOUND;
  }
  if (std::find_if(instances.begin(), instances.end(), [cid](auto in) { return in.cid == cid; }) ==
      instances.end()) {
    std::cerr << "No existing instances in env " << env_id << " with cid " << cid << '\n';
    return ZX_ERR_NOT_FOUND;
  }

  fuchsia::virtualization::HostVsockEndpointSyncPtr vsock_endpoint;
  realm->GetHostVsockEndpoint(vsock_endpoint.NewRequest());

  // Open a socket to the guest's vsock port where vshd should be listening
  zx::socket socket, remote_socket;
  status = zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket);
  if (status != ZX_OK) {
    std::cerr << "Failed to create socket: " << zx_status_get_string(status) << '\n';
    return status;
  }
  vsock_endpoint->Connect(cid, port, std::move(remote_socket), &status);
  if (status != ZX_OK) {
    std::cerr << "Failed to connect: " << zx_status_get_string(status) << '\n';
    return status;
  }

  // Now |socket| is a zircon socket plumbed to a port on the guest's vsock
  // interface. The vshd service is hopefully on the other end of this pipe.
  // We communicate with the service via protobuf messages.
  if (!init_shell(socket)) {
    std::cerr << "vsh SetupConnection failed.";
    return ZX_ERR_INTERNAL;
  }
  // Reset the TTY when the connection closes.
  auto cleanup = fit::defer([]() { reset_tty(); });

  // Directly inject some helper functions for connecting to container.
  // This sleep below is to give bash some time to start after being `exec`d.
  // Otherwise the input will be duplicated in the output stream.
  usleep(100'000);
  vm_tools::vsh::GuestMessage msg_out;
  msg_out.mutable_data_message()->set_stream(vm_tools::vsh::STDIN_STREAM);
  msg_out.mutable_data_message()->set_data(
      "function buster() { lxc exec buster -- login -f machina ; } \n\n");
  if (!vsh::SendMessage(socket, msg_out)) {
    std::cerr << "Warning: Failed to inject helper function.\n";
  }

  // Set up the I/O loops
  ConsoleIn i(loop, zx::unowned_socket(socket));
  ConsoleOut o(loop, zx::unowned_socket(socket));

  if (!i.Start()) {
    std::cerr << "Problem starting ConsoleIn loop.\n";
    return ZX_ERR_INTERNAL;
  }
  if (!o.Start()) {
    std::cerr << "Problem starting ConsoleOut loop.\n";
    return ZX_ERR_INTERNAL;
  }

  return loop->Run();
}
