// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <threads.h>

#include <map>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/fxl/strings/trim.h"
#include "src/lib/testing/predicates/status.h"
#include "src/virtualization/lib/grpc/fdio_util.h"
#include "src/virtualization/lib/guest_interaction/client/client_impl.h"
#include "src/virtualization/lib/guest_interaction/common.h"
#include "src/virtualization/lib/guest_interaction/test/integration_test_lib.h"
#include "src/virtualization/tests/lib/guest_console.h"

namespace {

using ::fuchsia::virtualization::HostVsockEndpoint_Connect_Result;

constexpr size_t kBufferSize = 100;

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

static void ConvertSocketToNonBlockingFd(zx::socket socket, fbl::unique_fd& fd) {
  ASSERT_OK(fdio_fd_create(socket.release(), fd.reset_and_get_address()));
  int result;
  ASSERT_EQ((result = SetNonBlocking(fd)), 0) << strerror(result);
}

TEST_F(GuestInteractionTest, GrpcExecScriptTest) {
  // Connect the gRPC client to the guest under test.
  fuchsia::virtualization::HostVsockEndpointPtr ep;
  GetHostVsockEndpoint(ep.NewRequest());

  zx::socket local_socket;
  std::optional<zx_status_t> status;
  auto callback = [&status, &local_socket](HostVsockEndpoint_Connect_Result result) {
    if (result.is_response()) {
      local_socket = std::move(result.response().socket);
      status = ZX_OK;
    } else {
      status = result.err();
    }
  };
  ep->Connect(GUEST_INTERACTION_PORT, std::move(callback));

  RunLoopUntil([&status]() { return status.has_value(); });
  ASSERT_OK(status.value());

  fbl::unique_fd vsock_fd;
  ASSERT_NO_FATAL_FAILURE(ConvertSocketToNonBlockingFd(std::move(local_socket), vsock_fd));

  ClientImpl<PosixPlatform> client(vsock_fd.release());

  // Push the bash script to the guest.
  fidl::InterfaceHandle<fuchsia::io::File> put_remote;
  ASSERT_OK(fdio_open(kTestScriptSource,
                      static_cast<uint32_t>(fuchsia::io::OpenFlags::RIGHT_READABLE),
                      put_remote.NewRequest().TakeChannel().release()));

  status.reset();
  client.Put(std::move(put_remote), kGuestScriptDestination,
             [&client, &status](zx_status_t put_result) {
               status = put_result;
               client.Stop();
             });
  client.Run();
  ASSERT_TRUE(status.has_value());
  ASSERT_OK(status.value());

  // Run the bash script in the guest.  The script will write to stdout and
  // stderr.  The script will also block waiting to receive input from stdin.
  zx::socket stdin_writer, stdin_reader;
  ASSERT_OK(zx::socket::create(0, &stdin_writer, &stdin_reader));

  zx::socket stdout_writer, stdout_reader;
  ASSERT_OK(zx::socket::create(0, &stdout_writer, &stdout_reader));

  zx::socket stderr_writer, stderr_reader;
  ASSERT_OK(zx::socket::create(0, &stderr_writer, &stderr_reader));

  // Once the subprocess has started, write to stdin.
  std::string to_write = kTestScriptInput;
  uint32_t bytes_written = 0;
  fbl::unique_fd stdin_fd;
  ASSERT_NO_FATAL_FAILURE(ConvertSocketToNonBlockingFd(std::move(stdin_writer), stdin_fd));

  // Run the bash script on the guest.
  std::string command = "/bin/sh ";
  command.append(kGuestScriptDestination);
  std::map<std::string, std::string> env_vars{{"STDOUT_STRING", kTestStdout},
                                              {"STDERR_STRING", kTestStderr}};
  std::string std_out;
  std::string std_err;
  std::optional<zx_status_t> exec_started;
  std::optional<zx_status_t> exec_terminated;
  int32_t ret_code = -1;

  fuchsia::netemul::guest::CommandListenerPtr listener;
  listener.events().OnStarted = [&](zx_status_t status) {
    exec_started = status;
    if (status == ZX_OK) {
      while (bytes_written < to_write.size()) {
        size_t curr_bytes_written = write(stdin_fd.get(), &(to_write.c_str()[bytes_written]),
                                          to_write.size() - bytes_written);
        if (curr_bytes_written < 0) {
          break;
        }

        bytes_written += curr_bytes_written;
      }
    }
    client.Stop();
    stdin_fd.reset();
  };
  listener.events().OnTerminated = [&](zx_status_t exec_result, int32_t exit_code) {
    exec_terminated = exec_result;
    ret_code = exit_code;

    std_out = drain_socket(std::move(stdout_reader));
    std_err = drain_socket(std::move(stderr_reader));
    client.Stop();
  };

  client.Exec(command, env_vars, std::move(stdin_reader), std::move(stdout_writer),
              std::move(stderr_writer), listener.NewRequest(), dispatcher());

  // Ensure that the process started cleanly.
  {
    thrd_t client_run_thread;
    ASSERT_EQ(client.Start(client_run_thread), thrd_success);
    RunLoopUntil([&exec_started] { return exec_started.has_value(); });
    int32_t thread_ret_code;
    ASSERT_EQ(thrd_join(client_run_thread, &thread_ret_code), thrd_success);
    ASSERT_EQ(thread_ret_code, 0);
  }

  ASSERT_TRUE(exec_started.has_value());
  ASSERT_OK(exec_started.value());

  // Ensure the gRPC operation completed successfully and validate the stdout
  // and stderr.
  {
    thrd_t client_run_thread;
    ASSERT_EQ(client.Start(client_run_thread), thrd_success);
    RunLoopUntil([&exec_terminated] { return exec_terminated.has_value(); });
    int32_t thread_ret_code;
    ASSERT_EQ(thrd_join(client_run_thread, &thread_ret_code), thrd_success);
    ASSERT_EQ(thread_ret_code, 0);
  }

  ASSERT_TRUE(exec_terminated.has_value());
  ASSERT_OK(exec_terminated.value());
  ASSERT_EQ(fxl::TrimString(std_out, "\n"), std::string_view(kTestStdout));
  ASSERT_EQ(fxl::TrimString(std_err, "\n"), std::string_view(kTestStderr));

  // The bash script will create a file with contents that were written to
  // stdin.  Pull this file back and inspect its contents.
  fidl::InterfaceHandle<fuchsia::io::File> get_remote;
  ASSERT_OK(fdio_open(
      kHostOuputCopyLocation,
      static_cast<uint32_t>(fuchsia::io::OpenFlags::RIGHT_WRITABLE |
                            fuchsia::io::OpenFlags::CREATE | fuchsia::io::OpenFlags::TRUNCATE),
      get_remote.NewRequest().TakeChannel().release()));

  status.reset();
  client.Get(kGuestFileOutputLocation, std::move(get_remote), [&](zx_status_t get_result) {
    status = get_result;
    client.Stop();
  });
  client.Run();
  ASSERT_TRUE(status.has_value());
  ASSERT_OK(status.value());

  // Verify the contents that were communicated through stdin.
  std::string output_string;
  char output_buf[kBufferSize];
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(open(kHostOuputCopyLocation, O_RDONLY))) << strerror(errno);
  while (true) {
    ssize_t read_size = read(fd.get(), output_buf, kBufferSize);
    ASSERT_GE(read_size, 0) << strerror(errno);
    if (read_size == 0) {
      break;
    }
    output_string.append(output_buf, read_size);
  }

  ASSERT_STREQ(output_string.c_str(), kTestScriptInput);
}

// Create a file large enough that it needs to be fragmented when it is sent
// to and received from the guest and then send it to and retrieve it from the
// guest.
TEST_F(GuestInteractionTest, GrpcPutGetTest) {
  // Connect the gRPC client to the guest under test.
  fuchsia::virtualization::HostVsockEndpointPtr ep;
  GetHostVsockEndpoint(ep.NewRequest());

  zx::socket local_socket;
  std::optional<zx_status_t> status;
  auto callback = [&status, &local_socket](HostVsockEndpoint_Connect_Result result) {
    if (result.is_response()) {
      local_socket = std::move(result.response().socket);
      status = ZX_OK;
    } else {
      status = result.err();
    }
  };
  ep->Connect(GUEST_INTERACTION_PORT, std::move(callback));

  RunLoopUntil([&status] { return status.has_value(); });
  ASSERT_TRUE(status.has_value());
  ASSERT_OK(status.value());

  fbl::unique_fd vsock_fd;
  ASSERT_NO_FATAL_FAILURE(ConvertSocketToNonBlockingFd(std::move(local_socket), vsock_fd));

  ClientImpl<PosixPlatform> client(vsock_fd.release());

  // Write a file of gibberish that the test can send over to the guest.
  constexpr char test_file[] = "/tmp/test_file.txt";
  constexpr char guest_destination[] = "/root/new/directory/test_file.txt";
  constexpr char host_verification_file[] = "/tmp/verification_file.txt";

  std::string file_contents;
  for (int i = 0; i < 2 * CHUNK_SIZE; i++) {
    file_contents.push_back(static_cast<char>(i % ('z' - 'A') + 'A'));
  }
  fbl::unique_fd fd;
  ASSERT_TRUE(fd = fbl::unique_fd(open(test_file, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR)))
      << strerror(errno);
  uint32_t bytes_written = 0;
  while (bytes_written < file_contents.size()) {
    ssize_t write_size = write(fd.get(), file_contents.c_str() + bytes_written,
                               file_contents.size() - bytes_written);
    ASSERT_GT(write_size, 0);
    bytes_written += write_size;
  }

  // Push the test file to the guest
  fidl::InterfaceHandle<fuchsia::io::File> put_remote;
  ASSERT_OK(fdio_open(test_file, static_cast<uint32_t>(fuchsia::io::OpenFlags::RIGHT_READABLE),
                      put_remote.NewRequest().TakeChannel().release()));

  status.reset();
  client.Put(std::move(put_remote), guest_destination, [&](zx_status_t put_result) {
    status = put_result;
    client.Stop();
  });
  client.Run();
  ASSERT_TRUE(status.has_value());
  ASSERT_OK(status.value());

  // Copy back the file that was sent to the guest.
  fidl::InterfaceHandle<fuchsia::io::File> get_remote;
  ASSERT_OK(fdio_open(
      host_verification_file,
      static_cast<uint32_t>(fuchsia::io::OpenFlags::RIGHT_WRITABLE |
                            fuchsia::io::OpenFlags::CREATE | fuchsia::io::OpenFlags::TRUNCATE),
      get_remote.NewRequest().TakeChannel().release()));

  status.reset();
  client.Get(guest_destination, std::move(get_remote), [&](zx_status_t get_result) {
    status = get_result;
    client.Stop();
  });
  client.Run();
  ASSERT_TRUE(status.has_value());
  ASSERT_OK(status.value());

  // Verify the contents that were communicated through stdin.
  std::string verification_string;
  char output_buf[kBufferSize];
  fd.reset(open(host_verification_file, O_RDONLY));
  while (true) {
    ssize_t read_size = read(fd.get(), output_buf, kBufferSize);
    ASSERT_GE(read_size, 0) << strerror(errno);
    if (read_size == 0) {
      break;
    }
    verification_string.append(std::string(output_buf, read_size));
  }

  ASSERT_EQ(verification_string, file_contents);
}

}  // namespace
