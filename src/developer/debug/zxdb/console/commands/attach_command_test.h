// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_ATTACH_COMMAND_TEST_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_ATTACH_COMMAND_TEST_H_

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

// Shared test harness for logging attaches and filters for testing the job- and attach- related
// commands.

class AttachTestRemoteAPI : public RemoteAPI {
 public:
  struct AttachLog {
    debug_ipc::AttachRequest request;
    fit::callback<void(const Err&, debug_ipc::AttachReply)> cb;
  };

  void Attach(const debug_ipc::AttachRequest& request,
              fit::callback<void(const Err&, debug_ipc::AttachReply)> cb) override {
    last_attach = AttachLog{request, std::move(cb)};
  }

  void UpdateFilter(const debug_ipc::UpdateFilterRequest& request,
                    fit::callback<void(const Err&, debug_ipc::UpdateFilterReply)> cb) override {
    update_filter_requests.push_back(request);
  }

  // Stores the last one.
  std::optional<AttachLog> last_attach;

  // Stores a log of all requests (since the tests needs all of them).
  std::vector<debug_ipc::UpdateFilterRequest> update_filter_requests;
};

class AttachCommandTest : public ConsoleTest {
 public:
  AttachTestRemoteAPI* attach_remote_api() { return attach_remote_api_; }

  // Returns the last filter in the last UpdateFilter request.
  const debug_ipc::Filter& GetLastFilter() {
    loop().RunUntilNoTasks();  // Filter sync is asynchronous.
    FX_CHECK(!attach_remote_api_->update_filter_requests.empty());
    FX_CHECK(!attach_remote_api_->update_filter_requests.back().filters.empty());
    return attach_remote_api_->update_filter_requests.back().filters.back();
  }

 protected:
  // RemoteAPITest overrides.
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() override {
    auto remote_api = std::make_unique<AttachTestRemoteAPI>();
    attach_remote_api_ = remote_api.get();
    return remote_api;
  }

  AttachTestRemoteAPI* attach_remote_api_ = nullptr;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_COMMANDS_ATTACH_COMMAND_TEST_H_
