// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/net/stack/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/component/cpp/environment_services_helper.h>
#include <lib/component/cpp/testing/test_util.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/vfs.h>
#include <lib/fzl/fdio.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/sys/cpp/testing/enclosing_environment.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <unistd.h>

#include <map>

#include <src/lib/files/unique_fd.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/trim.h>
#include <src/virtualization/tests/guest_console.h>
#include <src/virtualization/tests/mock_netstack.h>

#include "gtest/gtest.h"
#include "src/virtualization/lib/grpc/fdio_util.h"
#include "src/virtualization/lib/guest_interaction/client/client_impl.h"
#include "src/virtualization/lib/guest_interaction/common.h"

static constexpr size_t kBufferSize = 100;

static constexpr char kGuestLabel[] = "debian_guest";
static constexpr char kGuestManagerUrl[] =
    "fuchsia-pkg://fuchsia.com/guest_manager#meta/guest_manager.cmx";
static constexpr char kDebianGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/debian_guest#meta/debian_guest.cmx";

// The host will copy kTestScriptSource to kGuestScriptDestination on the
// guest.  The host will then ask the guest to exec kGuestScriptDestination
// and feed kTestScriptInput to the guest process's stdin.  The script will
// will echo kTestStdout to stdout, kTestStderr to stderr, and kTestScriptInput
// to  kGuestFileOutputLocation.  The host will
// download the file to kHostOuputCopyLocation.
static constexpr char kTestScriptSource[] = "/pkg/data/test_script.sh";
static constexpr char kGuestScriptDestination[] = "/root/input/test_script.sh";
static constexpr char kTestStdout[] = "stdout";
static constexpr char kTestStderr[] = "stderr";
static constexpr char kTestScriptInput[] = "hello world\n";
static constexpr char kGuestFileOutputLocation[] = "/root/output/script_output.txt";
static constexpr char kHostOuputCopyLocation[] = "/data/copy";

std::string drain_socket(zx::socket socket) {
  std::string out_string;
  size_t bytes_read;
  char read_buf[kBufferSize];

  while (true) {
    zx_status_t read_status = socket.read(0, read_buf, kBufferSize, &bytes_read);
    if (read_status == ZX_ERR_SHOULD_WAIT) {
      continue;
    }
    if (read_status != ZX_OK) {
      break;
    }
    out_string.append(read_buf, bytes_read);
  }

  return out_string;
}

class GuestInteractionTest : public sys::testing::TestWithEnvironment {
 public:
  void CreateEnvironment() {
    ASSERT_TRUE(services_ && !env_);

    env_ = CreateNewEnclosingEnvironment("GuestInteractionEnvironment", std::move(services_));
  }

  void LaunchDebianGuest() {
    // Launch the Debian guest
    fuchsia::virtualization::LaunchInfo guest_launch_info;
    guest_launch_info.url = kDebianGuestUrl;
    guest_launch_info.args.emplace({"--virtio-gpu=false", "--virtio-net=true"});

    fuchsia::virtualization::ManagerPtr guest_environment_manager;
    fuchsia::virtualization::GuestPtr guest_instance_controller;
    cid_ = -1;

    env_->ConnectToService(guest_environment_manager.NewRequest());
    guest_environment_manager->Create(kGuestLabel, realm_.NewRequest());
    realm_->LaunchInstance(std::move(guest_launch_info), guest_instance_controller.NewRequest(),
                           [&](uint32_t callback_cid) { cid_ = callback_cid; });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([this]() { return cid_ >= 0; }, zx::sec(5)));

    // Start a GuestConsole.  When the console starts, it waits until it
    // receives some sensible output from the guest to ensure that the guest is
    // usable.
    zx::socket socket;
    guest_instance_controller->GetSerial([&socket](zx::socket s) { socket = std::move(s); });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&socket] { return socket.is_valid(); }, zx::sec(30)));

    GuestConsole serial(std::make_unique<ZxSocket>(std::move(socket)));
    zx_status_t status = serial.Start();
    ASSERT_TRUE(status == ZX_OK);
  }

  uint32_t cid_;
  fuchsia::virtualization::RealmPtr realm_;
  std::unique_ptr<sys::testing::EnvironmentServices> services_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> env_;
  MockNetstack mock_netstack_;

 protected:
  void SetUp() {
    services_ = CreateServices();

    // Add Netstack services
    services_->AddService(mock_netstack_.GetHandler(), fuchsia::netstack::Netstack::Name_);
    services_->AddService(mock_netstack_.GetHandler(), fuchsia::net::stack::Stack::Name_);

    // Add guest service
    fuchsia::sys::LaunchInfo guest_manager_launch_info;
    guest_manager_launch_info.url = kGuestManagerUrl;
    guest_manager_launch_info.out = sys::CloneFileDescriptor(1);
    guest_manager_launch_info.err = sys::CloneFileDescriptor(2);
    services_->AddServiceWithLaunchInfo(std::move(guest_manager_launch_info),
                                        fuchsia::virtualization::Manager::Name_);
  }
};

TEST_F(GuestInteractionTest, GrpcExecScriptTest) {
  CreateEnvironment();
  LaunchDebianGuest();

  // Connect the gRPC client to the guest under test.
  fuchsia::virtualization::HostVsockEndpointPtr ep;
  realm_->GetHostVsockEndpoint(ep.NewRequest());

  zx::socket local_socket, remote_socket;
  zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &local_socket, &remote_socket);
  ASSERT_EQ(status, ZX_OK);

  ep->Connect(cid_, GUEST_INTERACTION_PORT, std::move(remote_socket),
              [&status](zx_status_t connect_status) { status = connect_status; });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&status] { return status == ZX_OK; }, zx::sec(5)));

  int32_t vsock_fd = ConvertSocketToNonBlockingFd(std::move(local_socket));
  ASSERT_GE(vsock_fd, 0);

  ClientImpl<PosixPlatform> client(vsock_fd);

  // Push the bash script to the guest
  zx::channel put_local, put_remote;
  zx::channel::create(0, &put_local, &put_remote);
  zx_status_t open_status = fdio_open(kTestScriptSource, ZX_FS_RIGHT_READABLE, put_local.release());
  ASSERT_EQ(open_status, ZX_OK);

  OperationStatus transfer_status = OperationStatus::OK;
  client.Put(std::move(put_remote), kGuestScriptDestination, [&](OperationStatus put_result) {
    transfer_status = put_result;
    client.Stop();
  });
  client.Run();
  ASSERT_EQ(transfer_status, OperationStatus::OK);

  // Run the bash script in the guest.  The script will write to stdout and
  // stderr.  The script will also block waiting to receive input from stdin.
  zx::socket stdin_writer, stdin_reader;
  zx::socket::create(0, &stdin_writer, &stdin_reader);

  zx::socket stdout_writer, stdout_reader;
  zx::socket::create(0, &stdout_writer, &stdout_reader);

  zx::socket stderr_writer, stderr_reader;
  zx::socket::create(0, &stderr_writer, &stderr_reader);

  // Once the subprocess has started, write to stdin.
  std::string to_write = kTestScriptInput;
  uint32_t bytes_written = 0;
  int32_t stdin_fd = ConvertSocketToNonBlockingFd(std::move(stdin_writer));

  while (bytes_written < to_write.size()) {
    size_t curr_bytes_written =
        write(stdin_fd, &(to_write.c_str()[bytes_written]), to_write.size() - bytes_written);
    if (curr_bytes_written < 0) {
      break;
    }

    bytes_written += curr_bytes_written;
  }
  close(stdin_fd);

  // Run the bash script on the guest.
  std::string command = "/bin/sh ";
  command.append(kGuestScriptDestination);
  std::map<std::string, std::string> env_vars{{"STDOUT_STRING", kTestStdout},
                                              {"STDERR_STRING", kTestStderr}};
  std::string std_out;
  std::string std_err;
  OperationStatus exec_status = OperationStatus::OK;
  int32_t ret_code = -1;
  client.Exec(command, env_vars, std::move(stdin_reader), std::move(stdout_writer),
              std::move(stderr_writer), [&](OperationStatus exec_result, int32_t exit_code) {
                exec_status = exec_result;
                ret_code = exit_code;

                std_out = drain_socket(std::move(stdout_reader));
                std_err = drain_socket(std::move(stderr_reader));

                client.Stop();
              });

  client.Run();

  // Ensure the gRPC operation completed successfully and validate the stdout
  // and stderr.
  ASSERT_EQ(exec_status, OperationStatus::OK);
  ASSERT_EQ(fxl::TrimString(std_out, "\n"), fxl::StringView(kTestStdout));
  ASSERT_EQ(fxl::TrimString(std_err, "\n"), fxl::StringView(kTestStderr));

  // The bash script will create a file with contents that were written to
  // stdin.  Pull this file back and inspect its contents.
  zx::channel get_local, get_remote;
  zx::channel::create(0, &get_local, &get_remote);
  open_status = fdio_open(kHostOuputCopyLocation,
                          ZX_FS_RIGHT_WRITABLE | ZX_FS_FLAG_CREATE | ZX_FS_FLAG_TRUNCATE,
                          get_local.release());
  ASSERT_EQ(open_status, ZX_OK);

  transfer_status = OperationStatus::OK;
  client.Get(kGuestFileOutputLocation, std::move(get_remote), [&](OperationStatus get_result) {
    transfer_status = get_result;
    client.Stop();
  });
  client.Run();
  ASSERT_EQ(transfer_status, OperationStatus::OK);

  // Verify the contents that were communicated through stdin.
  std::string output_string;
  char output_buf[kBufferSize];
  fbl::unique_fd fd = fbl::unique_fd(open(kHostOuputCopyLocation, O_RDONLY));
  while (true) {
    size_t read_size = read(fd.get(), output_buf, kBufferSize);
    if (read_size <= 0)
      break;
    output_string.append(output_buf, read_size);
  }

  ASSERT_STREQ(output_string.c_str(), kTestScriptInput);
}

// Create a file large enough that it needs to be fragmented when it is sent
// to and received from the guest and then send it to and retrieve it from the
// guest.
TEST_F(GuestInteractionTest, GrpcPutGetTest) {
  CreateEnvironment();
  LaunchDebianGuest();

  // Connect the gRPC client to the guest under test.
  fuchsia::virtualization::HostVsockEndpointPtr ep;
  realm_->GetHostVsockEndpoint(ep.NewRequest());

  zx::socket local_socket, remote_socket;
  zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &local_socket, &remote_socket);
  ASSERT_EQ(status, ZX_OK);

  ep->Connect(cid_, GUEST_INTERACTION_PORT, std::move(remote_socket),
              [&status](zx_status_t connect_status) { status = connect_status; });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&status] { return status == ZX_OK; }, zx::sec(5)));

  int32_t vsock_fd = ConvertSocketToNonBlockingFd(std::move(local_socket));
  ASSERT_GE(vsock_fd, 0);

  ClientImpl<PosixPlatform> client(vsock_fd);

  // Write a file of gibberish that the test can send over to the guest.
  char test_file[] = "/data/test_file.txt";
  char guest_destination[] = "/root/new/directory/test_file.txt";
  char host_verification_file[] = "/data/verification_file.txt";

  std::string file_contents;
  for (int i = 0; i < 2 * CHUNK_SIZE; i++) {
    file_contents.push_back(i % (('z' - 'A') + 'A'));
  }
  fbl::unique_fd fd = fbl::unique_fd(open(test_file, O_WRONLY | O_TRUNC | O_CREAT));
  uint32_t bytes_written = 0;
  while (bytes_written < file_contents.size()) {
    ssize_t write_size = write(fd.get(), file_contents.c_str() + bytes_written,
                               file_contents.size() - bytes_written);
    ASSERT_TRUE(write_size > 0);
    bytes_written += write_size;
  }

  // Push the test file to the guest
  zx::channel put_local, put_remote;
  zx::channel::create(0, &put_local, &put_remote);
  zx_status_t open_status = fdio_open(test_file, ZX_FS_RIGHT_READABLE, put_local.release());
  ASSERT_EQ(open_status, ZX_OK);

  OperationStatus transfer_status = OperationStatus::OK;
  client.Put(std::move(put_remote), guest_destination, [&](OperationStatus put_result) {
    transfer_status = put_result;
    client.Stop();
  });
  client.Run();
  ASSERT_EQ(transfer_status, OperationStatus::OK);

  // Copy back the file that was sent to the guest.
  zx::channel get_local, get_remote;
  zx::channel::create(0, &get_local, &get_remote);
  open_status = fdio_open(host_verification_file,
                          ZX_FS_RIGHT_WRITABLE | ZX_FS_FLAG_CREATE | ZX_FS_FLAG_TRUNCATE,
                          get_local.release());
  ASSERT_EQ(open_status, ZX_OK);

  transfer_status = OperationStatus::OK;
  client.Get(guest_destination, std::move(get_remote), [&](OperationStatus get_result) {
    transfer_status = get_result;
    client.Stop();
  });
  client.Run();
  ASSERT_EQ(transfer_status, OperationStatus::OK);

  // Verify the contents that were communicated through stdin.
  std::string verification_string;
  char output_buf[kBufferSize];
  fd.reset(open(host_verification_file, O_RDONLY));
  while (true) {
    size_t read_size = read(fd.get(), output_buf, kBufferSize);
    if (read_size <= 0)
      break;
    verification_string.append(std::string(output_buf, read_size));
  }

  ASSERT_EQ(verification_string, file_contents);
}
