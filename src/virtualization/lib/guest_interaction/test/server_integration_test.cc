// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/vfs.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/syslog/cpp/macros.h>
#include <threads.h>

#include <map>

#include <gtest/gtest.h>
#include <src/lib/files/unique_fd.h>
#include <src/virtualization/tests/fake_netstack.h>
#include <src/virtualization/tests/guest_console.h>

#include "src/lib/fxl/strings/trim.h"
#include "src/virtualization/lib/grpc/fdio_util.h"
#include "src/virtualization/lib/guest_interaction/client/client_impl.h"
#include "src/virtualization/lib/guest_interaction/common.h"
#include "src/virtualization/lib/guest_interaction/test/integration_test_lib.h"

static constexpr size_t kBufferSize = 100;

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

static int run_grpc_client(void* client_to_run) {
  ClientImpl<PosixPlatform>* client_impl = (ClientImpl<PosixPlatform>*)client_to_run;
  client_impl->Run();
  return 0;
}

TEST_F(GuestInteractionTest, GrpcExecScriptTest) {
  CreateEnvironment();
  LaunchDebianGuest();

  // Connect the gRPC client to the guest under test.
  fuchsia::virtualization::HostVsockEndpointPtr ep;
  realm_->GetHostVsockEndpoint(ep.NewRequest());

  zx::socket local_socket, remote_socket;
  zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &local_socket, &remote_socket);
  ASSERT_EQ(status, ZX_OK);

  bool connect_complete = false;
  ep->Connect(cid_, GUEST_INTERACTION_PORT, std::move(remote_socket),
              [&connect_complete, &status](zx_status_t connect_status) {
                status = connect_status;
                connect_complete = true;
              });
  RunLoopUntil([&connect_complete] { return connect_complete; });
  ASSERT_EQ(status, ZX_OK);

  int32_t vsock_fd = ConvertSocketToNonBlockingFd(std::move(local_socket));
  ASSERT_GE(vsock_fd, 0);

  ClientImpl<PosixPlatform> client(vsock_fd);

  // Push the bash script to the guest
  zx::channel put_local, put_remote;
  zx::channel::create(0, &put_local, &put_remote);
  zx_status_t open_status =
      fdio_open(kTestScriptSource, fuchsia::io::OPEN_RIGHT_READABLE, put_local.release());
  ASSERT_EQ(open_status, ZX_OK);

  zx_status_t transfer_status = ZX_ERR_IO;
  client.Put(std::move(put_remote), kGuestScriptDestination, [&](zx_status_t put_result) {
    transfer_status = put_result;
    client.Stop();
  });
  client.Run();
  ASSERT_EQ(transfer_status, ZX_OK);

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

  // Run the bash script on the guest.
  std::string command = "/bin/sh ";
  command.append(kGuestScriptDestination);
  std::map<std::string, std::string> env_vars{{"STDOUT_STRING", kTestStdout},
                                              {"STDERR_STRING", kTestStderr}};
  std::string std_out;
  std::string std_err;
  zx_status_t exec_started_status = ZX_ERR_PEER_CLOSED;
  zx_status_t exec_terminated_status = ZX_ERR_PEER_CLOSED;
  bool exec_started = false;
  bool exec_terminated = false;
  int32_t ret_code = -1;

  fuchsia::netemul::guest::CommandListenerPtr listener;
  listener.events().OnStarted = [&](zx_status_t status) {
    exec_started_status = status;
    if (status == ZX_OK) {
      while (bytes_written < to_write.size()) {
        size_t curr_bytes_written =
            write(stdin_fd, &(to_write.c_str()[bytes_written]), to_write.size() - bytes_written);
        if (curr_bytes_written < 0) {
          break;
        }

        bytes_written += curr_bytes_written;
      }
    }
    client.Stop();
    close(stdin_fd);
    exec_started = true;
  };
  listener.events().OnTerminated = [&](zx_status_t exec_result, int32_t exit_code) {
    exec_terminated_status = exec_result;
    ret_code = exit_code;

    std_out = drain_socket(std::move(stdout_reader));
    std_err = drain_socket(std::move(stderr_reader));
    client.Stop();
    exec_terminated = true;
  };

  client.Exec(command, env_vars, std::move(stdin_reader), std::move(stdout_writer),
              std::move(stderr_writer), listener.NewRequest());

  // Ensure that the process started cleanly.
  thrd_t client_run_thread;
  thrd_create_with_name(&client_run_thread, run_grpc_client, &client, "gRPC run");
  RunLoopUntil([&exec_started] { return exec_started; });
  int32_t thread_ret_code;
  thrd_join(client_run_thread, &thread_ret_code);

  ASSERT_EQ(exec_started_status, ZX_OK);

  // Ensure the gRPC operation completed successfully and validate the stdout
  // and stderr.
  thrd_create_with_name(&client_run_thread, run_grpc_client, &client, "gRPC run");
  RunLoopUntil([&exec_terminated] { return exec_terminated; });
  thrd_join(client_run_thread, &thread_ret_code);

  ASSERT_EQ(exec_terminated_status, ZX_OK);
  ASSERT_EQ(fxl::TrimString(std_out, "\n"), std::string_view(kTestStdout));
  ASSERT_EQ(fxl::TrimString(std_err, "\n"), std::string_view(kTestStderr));

  // The bash script will create a file with contents that were written to
  // stdin.  Pull this file back and inspect its contents.
  zx::channel get_local, get_remote;
  zx::channel::create(0, &get_local, &get_remote);
  open_status = fdio_open(kHostOuputCopyLocation,
                          fuchsia::io::OPEN_RIGHT_WRITABLE | fuchsia::io::OPEN_FLAG_CREATE |
                              fuchsia::io::OPEN_FLAG_TRUNCATE,
                          get_local.release());
  ASSERT_EQ(open_status, ZX_OK);

  transfer_status = ZX_ERR_IO;
  client.Get(kGuestFileOutputLocation, std::move(get_remote), [&](zx_status_t get_result) {
    transfer_status = get_result;
    client.Stop();
  });
  client.Run();
  ASSERT_EQ(transfer_status, ZX_OK);

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
// TODO(fxbug.dev/43569): Flaky.
TEST_F(GuestInteractionTest, DISABLED_GrpcPutGetTest) {
  CreateEnvironment();
  LaunchDebianGuest();

  // Connect the gRPC client to the guest under test.
  fuchsia::virtualization::HostVsockEndpointPtr ep;
  realm_->GetHostVsockEndpoint(ep.NewRequest());

  zx::socket local_socket, remote_socket;
  zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &local_socket, &remote_socket);
  ASSERT_EQ(status, ZX_OK);

  bool connect_complete = false;
  ep->Connect(cid_, GUEST_INTERACTION_PORT, std::move(remote_socket),
              [&connect_complete, &status](zx_status_t connect_status) {
                status = connect_status;
                connect_complete = true;
              });
  RunLoopUntil([&connect_complete] { return connect_complete; });
  ASSERT_EQ(status, ZX_OK);

  int32_t vsock_fd = ConvertSocketToNonBlockingFd(std::move(local_socket));
  ASSERT_GE(vsock_fd, 0);

  ClientImpl<PosixPlatform> client(vsock_fd);

  // Write a file of gibberish that the test can send over to the guest.
  char test_file[] = "/data/test_file.txt";
  char guest_destination[] = "/root/new/directory/test_file.txt";
  char host_verification_file[] = "/data/verification_file.txt";

  std::string file_contents;
  for (int i = 0; i < 2 * CHUNK_SIZE; i++) {
    file_contents.push_back(i % ('z' - 'A') + 'A');
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
  zx_status_t open_status =
      fdio_open(test_file, fuchsia::io::OPEN_RIGHT_READABLE, put_local.release());
  ASSERT_EQ(open_status, ZX_OK);

  zx_status_t transfer_status = ZX_ERR_IO;
  client.Put(std::move(put_remote), guest_destination, [&](zx_status_t put_result) {
    transfer_status = put_result;
    client.Stop();
  });
  client.Run();
  ASSERT_EQ(transfer_status, ZX_OK);

  // Copy back the file that was sent to the guest.
  zx::channel get_local, get_remote;
  zx::channel::create(0, &get_local, &get_remote);
  open_status = fdio_open(host_verification_file,
                          fuchsia::io::OPEN_RIGHT_WRITABLE | fuchsia::io::OPEN_FLAG_CREATE |
                              fuchsia::io::OPEN_FLAG_TRUNCATE,
                          get_local.release());
  ASSERT_EQ(open_status, ZX_OK);

  transfer_status = ZX_ERR_IO;
  client.Get(guest_destination, std::move(get_remote), [&](zx_status_t get_result) {
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
