// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_REMOTE_API_TEST_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_REMOTE_API_TEST_H_

#include <initializer_list>
#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/shared/test_stream_buffer.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"

namespace zxdb {

class Frame;
class MockRemoteAPI;
class Process;
class Session;
class Thread;

// This is a test harness for client tests that mock out the RemoteAPI. This class sets up a message
// loop and the necessary plumbing.
//
// The individual tests supply their own implementation of RemoteAPI.
class RemoteAPITest : public TestWithLoop {
 public:
  // testing::Test implementation.
  void SetUp() override;
  void TearDown() override;

  Session& session() { return *session_; }

  // Returns the MockRemoteAPI constructed by the default implementation of GetRemoteAPIImpl()
  // below.
  //
  // Subclasses can provide any remote API implementation, but most tests want to use the standard
  // MockRemoteAPI. When GetRemoteAPIImpl() has not been overridden, it will create a MockRemoteAPI
  // and this getter will return it.
  //
  // If a derived implementation overrides GetRemoteAPIImpl(), this will return null. Such tests
  // should provide their own getter for their specific implementation.
  MockRemoteAPI* mock_remote_api() const { return mock_remote_api_; }

  // Makes the target have a fake running process with the given KOID. This assumes there is only
  // one target in the system and that it is not currently running.
  Process* InjectProcess(uint64_t process_koid);

  // Sends a "thread created" notifications to the client for the given fake KOID. The thread will
  // be reported as running.
  Thread* InjectThread(uint64_t process_koid, uint64_t thread_koid);

  // Sends the exception notification to the session.
  void InjectException(const debug_ipc::NotifyException& exception);

  // Sends the exception notification and forces the given stack information. This bypasses the
  // normal thread metadata computation. The exception address will be taken from the address of the
  // top of the stack.
  //
  // If you use the one that takes a NotifyException, the calling code need not populate the thread
  // vector and stack amount, they will be ignored.
  void InjectExceptionWithStack(const debug_ipc::NotifyException& exception,
                                std::vector<std::unique_ptr<Frame>> frames, bool has_all_frames);
  void InjectExceptionWithStack(uint64_t process_koid, uint64_t thread_koid,
                                debug_ipc::ExceptionType exception_type,
                                std::vector<std::unique_ptr<Frame>> frames, bool has_all_frames,
                                const std::vector<debug_ipc::BreakpointStats>& breakpoints = {});

 protected:
  // Constructs the remote API implementation for this test.
  //
  // Derived classes can override this to provide their own IPC mocks. Ownership will be transferred
  // to the Session so it will be valid until TearDown (most implementations will want to keep a
  // pointer).
  //
  // The default implementation will construct a MockRemoteAPI which will be available from
  // mock_remote_api();
  virtual std::unique_ptr<RemoteAPI> GetRemoteAPIImpl();

  // Allows tests to override the architecture for the test to run in. Defaults to x64.
  virtual debug_ipc::Arch GetArch() const { return debug_ipc::Arch::kX64; }

 private:
  std::unique_ptr<Session> session_;
  MockRemoteAPI* mock_remote_api_ = nullptr;  // See getter above. Owned by System.
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_REMOTE_API_TEST_H_
