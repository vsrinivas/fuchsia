// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a simple Fuchsia program that connects to the test_runner process,
// starts a test and exits with success or failure based on the success or
// failure of the test.

#include "apps/modular/src/test_runner/test_runner_client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <rapidjson/document.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include "lib/ftl/files/file.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"
#include "lib/ftl/strings/string_printf.h"

namespace modular {
namespace testing {

constexpr uint16_t kTestRunnerPort = 8342;      // TCP port
constexpr int kReadTimeoutMillis = 120 * 1000;  // Read timeout in milliseconds

bool TestRunnerClient::RunTest(const std::string& name,
                               const std::string& command_line) {
  // Connect to the test_runner on localhost.
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(kTestRunnerPort);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int sock = socket(addr.sin_family, SOCK_STREAM, IPPROTO_TCP);
  FTL_CHECK(sock != -1);
  ftl::UniqueFD unique_fd(sock);  // Automatically close the socket.
  int connected = connect(sock, (const struct sockaddr*)&addr, sizeof(addr));
  FTL_CHECK(connected != -1);

  // Generate a test id.
  // TODO(ianloic): use something more random.
  std::string test_id = ftl::StringPrintf("t%ld_%d", time(NULL), getpid());

  // Contruct the command to send to test_runner.
  std::string run_command =
      ftl::StringPrintf("run %s %s\n", test_id.c_str(), command_line.c_str());

  // Send the command to test_runner.
  int len = send(sock, run_command.c_str(), run_command.size(), 0);
  FTL_CHECK(len == (int)run_command.size());

  // Read response lines from test_runner.
  char buf[256];
  struct pollfd pfd = {
      .fd = sock, .events = POLLIN,
  };
  std::string line_buf;
  // TODO(ianloic): add a global timeout.
  for (;;) {
    int nfds = poll(&pfd, 1, kReadTimeoutMillis);
    if (nfds < 0) {
      // An error occurred in poll.
      perror("poll");
      return false;
    }
    if (nfds == 0) {
      // Timed out.
      FTL_LOG(ERROR) << "Test timed out";
      return false;
    }

    // Read some bytes from the network onto a buffer.
    len = recv(sock, buf, sizeof(buf) - 1, 0);
    FTL_CHECK(len >= 0);
    line_buf.append(std::string(buf, len));

    // Process lines from the buffer.
    for (;;) {
      size_t off = line_buf.find('\n');
      if (off == std::string::npos) {
        // No \n, keep reading.
        break;
      }

      // Parse the next line out.
      std::string line = line_buf.substr(0, off);
      line_buf.assign(line_buf.substr(off + 1));

      // Split the line according to the rough test_runner protocol.
      auto line_pieces =
          ftl::SplitString(line, " ", ftl::kTrimWhitespace, ftl::kSplitWantAll);
      FTL_CHECK(line_pieces.size() >= 3);
      FTL_CHECK(line_pieces[0] == test_id);
      if (line_pieces[1] == "teardown") {
        // Test has completed - check for failure.
        return line_pieces[2] == "pass";
      }
    }
  }
}

bool TestRunnerClient::RunTests(const std::string& json_path) {
  std::string json;
  FTL_CHECK(files::ReadFileToString(json_path, &json));

  rapidjson::Document doc;
  doc.Parse(json);
  FTL_CHECK(doc.IsObject());

  auto& tests = doc["tests"];
  FTL_CHECK(tests.IsArray());

  for (auto& test : tests.GetArray()) {
    FTL_CHECK(test.IsObject());
    std::string test_name = test["name"].GetString();
    std::string test_exec = test["exec"].GetString();
    FTL_LOG(INFO) << "Asking test_runner to run test: " << test_name;
    time_t start_time = time(NULL);
    if (!RunTest(test_name, test_exec)) {
      FTL_LOG(ERROR) << "Test " << test_name << " failed in "
                     << (time(NULL) - start_time) << "s.";
      return 1;
    }
    FTL_LOG(INFO) << "Test " << test_name << " succeeded in "
                  << (time(NULL) - start_time) << "s.";
  }

  return 0;
}

}  // namespace testing
}  // namespace modular
