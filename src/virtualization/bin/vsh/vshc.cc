// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vsh/vshc.h"

#include <fcntl.h>
#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fdio/fd.h>
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
#include "src/lib/fxl/strings/string_printf.h"
#include "src/virtualization/lib/vsh/util.h"
#include "src/virtualization/third_party/vm_tools/vsh.pb.h"

namespace fpty = fuchsia_hardware_pty;

using ::fuchsia::virtualization::HostVsockEndpoint_Connect_Result;

std::optional<fpty::wire::WindowSize> get_window_size(zx::unowned_channel pty) {
  auto result = fidl::WireCall<fpty::Device>(pty)->GetWindowSize();

  if (!result.ok()) {
    std::cerr << "Call to GetWindowSize failed: " << result << std::endl;
    return std::nullopt;
  }

  if (result.value().status != ZX_OK) {
    std::cerr << "GetWindowSize returned with status: " << result.value().status << std::endl;
    return std::nullopt;
  }

  return result.value().size;
}

std::pair<int, int> init_tty() {
  int cols = 80;
  int rows = 24;

  if (isatty(STDIN_FILENO)) {
    fdio_t* io = fdio_unsafe_fd_to_io(STDIN_FILENO);
    auto wsz = get_window_size(zx::unowned_channel(fdio_unsafe_borrow_channel(io)));

    if (!wsz) {
      std::cerr << "Warning: Unable to determine shell geometry, defaulting to 80x24.\n";
    } else {
      cols = wsz->width;
      rows = wsz->height;
    }

    // Enable raw mode on tty so that inputs such as ctrl-c are passed on
    // faithfully to the client for forwarding to the remote shell
    // (instead of closing the client side).
    auto result = fidl::WireCall<fpty::Device>(zx::unowned_channel(fdio_unsafe_borrow_channel(io)))
                      ->ClrSetFeature(0, fpty::wire::kFeatureRaw);

    if (result.status() != ZX_OK || result.value().status != ZX_OK) {
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
                      ->ClrSetFeature(fpty::wire::kFeatureRaw, 0);

    if (result.status() != ZX_OK || result.value().status != ZX_OK) {
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

    // If stdin is a tty then set up a handler for OOB events.
    if (isatty(STDIN_FILENO)) {
      auto io = fdio_unsafe_fd_to_io(STDIN_FILENO);
      auto result =
          fidl::WireCall<fpty::Device>(zx::unowned_channel(fdio_unsafe_borrow_channel(io)))
              ->Describe2();
      fdio_unsafe_release(io);

      if (!result.ok()) {
        std::cerr << "Unable to get stdin channel description: " << result << std::endl;
        return false;
      }
      auto& info = result.value();
      FX_DCHECK(info.has_event()) << "stdin expected to have event";

      events_ = std::move(info.event());
      pty_event_waiter_.set_object(events_.get());
      pty_event_waiter_.set_trigger(static_cast<zx_signals_t>(fpty::wire::kSignalEvent));
      auto status = pty_event_waiter_.Begin(loop_->dispatcher());
      if (status != ZX_OK) {
        std::cerr << "Unable to start the pty event waiter due to: " << status << std::endl;
        return false;
      }
    }

    return true;
  }

 private:
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

  void HandleEvents(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                    const zx_packet_signal_t* signal) {
    if (status != ZX_OK && status != ZX_ERR_SHOULD_WAIT) {
      loop_->Shutdown();
      loop_->Quit();
      return;
    }

    FX_DCHECK(signal->observed & static_cast<zx_signals_t>(fpty::wire::kSignalEvent))
        << "Did not receive expected signal. Received: " << signal->observed;

    // Even if we exit early due to error still want to queue up the next instance of the handler.
    auto queue_next = fit::defer([wait, dispatcher] { wait->Begin(dispatcher); });

    // Get the channel backing stdin to use its pty.Device interface.
    fdio_t* io = fdio_unsafe_fd_to_io(STDIN_FILENO);
    zx::unowned_channel pty{fdio_unsafe_borrow_channel(io)};
    auto cleanup = fit::defer([io] { fdio_unsafe_release(io); });

    auto result = fidl::WireCall<fpty::Device>(pty)->ReadEvents();
    if (!result.ok()) {
      std::cerr << "Call to ReadEvents failed: " << result << std::endl;
      return;
    }
    if (result.value().status != ZX_OK) {
      std::cerr << "ReadEvents returned with status " << result.value().status << std::endl;
      return;
    }

    if (result.value().events & fpty::wire::kEventWindowSize) {
      auto ws = get_window_size(std::move(pty));
      if (!ws) {
        return;
      }

      vm_tools::vsh::GuestMessage msg_out;
      msg_out.mutable_resize_message()->set_rows(ws->height);
      msg_out.mutable_resize_message()->set_cols(ws->width);
      if (!vsh::SendMessage(*sink_, msg_out)) {
        std::cerr << "Failed to update window size.\n";
      }
    } else {
      // Leaving other events unhandled for now.
    }
  }

  async::Loop* loop_;
  zx::unowned_socket sink_;
  fsl::FDWaiter fd_waiter_;
  zx::eventpair events_{ZX_HANDLE_INVALID};
  async::WaitMethod<ConsoleIn, &ConsoleIn::HandleEvents> pty_event_waiter_{this};
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
          size_t bytes_written = 0;
          while (bytes_written < data.size()) {
            ssize_t actual =
                write(STDOUT_FILENO, data.data() + bytes_written, data.size() - bytes_written);
            FX_CHECK(actual != -1) << "Failed to write to stdout: " << errno;
            bytes_written += actual;
          }
          FX_DCHECK(bytes_written == data.size());
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

const char kCursorHide[] = "\x1b[?25l";
const char kCursorShow[] = "\x1b[?25h";
const char kColor0Normal[] = "\x1b[0m";
const char kColor1RedBright[] = "\x1b[1;31m";
const char kColor2GreenBright[] = "\x1b[1;32m";
const char kColor3Yellow[] = "\x1b[33m";
const char kColor5Purple[] = "\x1b[35m";
const char kEraseInLine[] = "\x1b[K";
const char kSpinner[] = "|/-\\";

int GetContainerStatusIndex(fuchsia::virtualization::ContainerStatus status) {
  switch (status) {
    case fuchsia::virtualization::ContainerStatus::TRANSIENT:
    case fuchsia::virtualization::ContainerStatus::LAUNCHING_GUEST:
      return 1;
    case fuchsia::virtualization::ContainerStatus::STARTING_VM:
      return 2;
    case fuchsia::virtualization::ContainerStatus::DOWNLOADING:
      return 4;
    case fuchsia::virtualization::ContainerStatus::EXTRACTING:
      return 6;
    case fuchsia::virtualization::ContainerStatus::STARTING:
      return 9;
    case fuchsia::virtualization::ContainerStatus::FAILED:
    case fuchsia::virtualization::ContainerStatus::READY:
      return 10;
  }
}

std::string GetContainerStatusString(const fuchsia::virtualization::LinuxGuestInfo& info) {
  switch (info.container_status()) {
    case fuchsia::virtualization::ContainerStatus::LAUNCHING_GUEST:
      return std::string("Initializing");
    case fuchsia::virtualization::ContainerStatus::STARTING_VM:
      return std::string("Starting the virtual machine");
    case fuchsia::virtualization::ContainerStatus::DOWNLOADING:
      return fxl::StringPrintf("Downloading the Linux container image (%d%%)",
                               info.download_percent());
    case fuchsia::virtualization::ContainerStatus::EXTRACTING:
      return std::string("Extracting the Linux container image");
    case fuchsia::virtualization::ContainerStatus::STARTING:
      return std::string("Starting the Linux container");
    case fuchsia::virtualization::ContainerStatus::TRANSIENT:
    case fuchsia::virtualization::ContainerStatus::FAILED:
    case fuchsia::virtualization::ContainerStatus::READY:
      return std::string();
  }
}

std::string MoveForward(int stage_index) { return fxl::StringPrintf("\x1b[%dC", stage_index); }

// Displays container startup status.
class ContainerStartup {
 public:
  void OnGuestStarted(const fuchsia::virtualization::LinuxGuestInfo& info) {
    container_status_ = info.container_status();
    if (container_status_ == fuchsia::virtualization::ContainerStatus::FAILED) {
      PrintAfterStage(kColor1RedBright, fxl::StringPrintf("Error starting guest: %s\r\n",
                                                          info.failure_reason().c_str()));
      Print(fxl::StringPrintf("%s%s", kColor0Normal, kCursorShow));
    } else if (container_status_ != fuchsia::virtualization::ContainerStatus::READY) {
      PrintStage(kColor3Yellow, GetContainerStatusString(info));
    }
  }

  void OnGuestInfoChanged(const fuchsia::virtualization::LinuxGuestInfo& info) {
    container_status_ = info.container_status();
    if (container_status_ == fuchsia::virtualization::ContainerStatus::FAILED) {
      PrintAfterStage(kColor1RedBright, fxl::StringPrintf("Failed to start container: %s\r\n",
                                                          info.failure_reason().c_str()));
      Print(fxl::StringPrintf("\r%s%s%s", kEraseInLine, kColor0Normal, kCursorShow));
    } else if (container_status_ == fuchsia::virtualization::ContainerStatus::READY) {
      PrintStage(kColor2GreenBright, "Ready\r\n");
      Print(fxl::StringPrintf("\r%s%s%s", kEraseInLine, kColor0Normal, kCursorShow));
    } else {
      PrintStage(kColor3Yellow, GetContainerStatusString(info));
    }
  }

  void PrintProgress() {
    if (container_status_ == fuchsia::virtualization::ContainerStatus::FAILED) {
      return;
    }
    InitializeProgress();
    int status_index = GetContainerStatusIndex(container_status_);
    Print(fxl::StringPrintf("\r%s%s%c", MoveForward(status_index).c_str(), kColor5Purple,
                            kSpinner[spinner_index_++ & 0x3]));
  }

  bool IsReady() { return container_status_ == fuchsia::virtualization::ContainerStatus::READY; }
  bool IsFailure() { return container_status_ == fuchsia::virtualization::ContainerStatus::FAILED; }

 private:
  void Print(const std::string& output) { std::cout << output << std::flush; }

  int GetStageIndexCount() {
    return GetContainerStatusIndex(fuchsia::virtualization::ContainerStatus::READY);
  }

  void InitializeProgress() {
    if (progress_initialized_) {
      return;
    }
    progress_initialized_ = true;
    Print(fxl::StringPrintf("%s%s[%s] ", kCursorHide, kColor5Purple,
                            std::string(GetStageIndexCount(), ' ').c_str()));
  }

  void PrintStage(const char* color, const std::string& output) {
    InitializeProgress();
    int status_index = GetContainerStatusIndex(container_status_);
    int status_index_count = GetStageIndexCount();
    std::string progress(status_index, '=');
    Print(fxl::StringPrintf("\r%s[%s%s%s%s%s ", kColor5Purple, progress.c_str(),
                            MoveForward(3 + (status_index_count - status_index)).c_str(),
                            kEraseInLine, color, output.c_str()));
    end_of_line_index_ = 4 + status_index_count + static_cast<int>(output.size());
  }

  void PrintAfterStage(const char* color, const std::string& output) {
    InitializeProgress();
    Print(fxl::StringPrintf("\r%s%s: %s", MoveForward(end_of_line_index_).c_str(), color,
                            output.c_str()));
    end_of_line_index_ += output.size();
  }

  bool progress_initialized_ = false;
  int spinner_index_ = 0;
  fuchsia::virtualization::ContainerStatus container_status_ =
      fuchsia::virtualization::ContainerStatus::FAILED;
  int end_of_line_index_ = 0;
};

}  // namespace

static bool init_shell(const zx::socket& usock, std::vector<std::string> args) {
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
  for (const auto& arg : args) {
    conn_req.add_argv(arg);
  }

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

zx_status_t handle_vsh(std::optional<uint32_t> o_port, std::vector<std::string> args,
                       async::Loop* loop, sys::ComponentContext* context) {
  uint32_t port = o_port.value_or(vsh::kVshPort);
  std::optional<std::string_view> linux_env_name;
  std::optional<uint32_t> linux_guest_cid;

  // This is hard-coded for now. A flag can be added if needed in the future.
  constexpr std::string_view kLinuxEnvironmentName("termina");

  // Wait for Linux environment to be ready if we have a non-empty
  // set of arguments.
  if (!args.empty()) {
    linux_env_name = kLinuxEnvironmentName;

    for (;;) {
      // Connect to the Linux manager.
      async::Loop linux_manager_loop(&kAsyncLoopConfigNeverAttachToThread);
      fuchsia::virtualization::LinuxManagerPtr linux_manager;
      zx_status_t status =
          context->svc()->Connect(linux_manager.NewRequest(linux_manager_loop.dispatcher()));
      if (status != ZX_OK) {
        std::cerr << "Unable to access /svc/" << fuchsia::virtualization::LinuxManager::Name_
                  << "\n";
        return status;
      }

      ContainerStartup container_startup;
      linux_manager.events().OnGuestInfoChanged =
          [&container_startup](std::string label, fuchsia::virtualization::LinuxGuestInfo info) {
            container_startup.OnGuestInfoChanged(info);
          };

      // Get the initial state of the container and start it if needed.
      linux_manager->StartAndGetLinuxGuestInfo(
          std::string(kLinuxEnvironmentName), [&container_startup, &linux_guest_cid](auto result) {
            linux_guest_cid = result.response().info.cid();
            container_startup.OnGuestStarted(result.response().info);
          });
      linux_manager_loop.Run(zx::time::infinite(), /*once*/ true);

      // Loop until container is ready. We intentionally continue on failure in
      // case we recover. It also gives the user a chance to see the error as
      // exiting might result in the terminal being closed.
      while (!container_startup.IsReady() && !container_startup.IsFailure()) {
        container_startup.PrintProgress();
        // 10 progress updates per second.
        linux_manager_loop.Run(zx::deadline_after(zx::msec(100)), /*once*/ true);
      }

      if (container_startup.IsReady()) {
        break;
      }
      std::cout << "Starting the Linux container has failed. Retry? (Y/n)" << std::endl;
      int c = std::getchar();
      switch (c) {
        case 'y':
        case 'Y':
        case '\n':
          continue;
        default:
          break;
      }
    }
  }

  fuchsia::virtualization::TerminaGuestManagerSyncPtr manager;
  context->svc()->Connect(manager.NewRequest());

  fuchsia::virtualization::GuestManager_Connect_Result get_guest_result;
  fuchsia::virtualization::GuestSyncPtr guest;
  manager->Connect(guest.NewRequest(), &get_guest_result);
  if (get_guest_result.is_err()) {
    return ZX_ERR_UNAVAILABLE;
  }

  fuchsia::virtualization::Guest_GetHostVsockEndpoint_Result get_vsock_result;
  fuchsia::virtualization::HostVsockEndpointSyncPtr vsock_endpoint;
  guest->GetHostVsockEndpoint(vsock_endpoint.NewRequest(), &get_vsock_result);
  if (get_vsock_result.is_err()) {
    std::cerr << "The vsock device is not present\n";
    return ZX_ERR_INVALID_ARGS;
  }

  HostVsockEndpoint_Connect_Result result;
  vsock_endpoint->Connect(port, &result);
  if (result.is_err()) {
    std::cerr << "Failed to connect: " << zx_status_get_string(result.err()) << '\n';
    return result.err();
  }
  zx::socket socket = std::move(result.response().socket);

  // Helper injection is likely undesirable if we aren't connecting to the default VM login shell.
  bool inject_helper = args.empty();

  // Now |socket| is a zircon socket plumbed to a port on the guest's vsock
  // interface. The vshd service is hopefully on the other end of this pipe.
  // We communicate with the service via protobuf messages.
  if (!init_shell(socket, std::move(args))) {
    std::cerr << "vsh SetupConnection failed.";
    return ZX_ERR_INTERNAL;
  }
  // Reset the TTY when the connection closes.
  auto cleanup = fit::defer([]() { reset_tty(); });

  if (inject_helper) {
    // Directly inject some helper functions for connecting to container.
    // This sleep below is to give bash some time to start after being `exec`d.
    // Otherwise the input will be duplicated in the output stream.
    usleep(100'000);
    vm_tools::vsh::GuestMessage msg_out;
    msg_out.mutable_data_message()->set_stream(vm_tools::vsh::STDIN_STREAM);
    msg_out.mutable_data_message()->set_data(
        "function penguin() { lxc exec penguin -- login -f machina ; } \n\n");
    if (!vsh::SendMessage(socket, msg_out)) {
      std::cerr << "Warning: Failed to inject helper function.\n";
    }
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
