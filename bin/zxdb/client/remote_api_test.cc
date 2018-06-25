// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/remote_api_test.h"

#include <inttypes.h>

#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system_impl.h"
#include "garnet/bin/zxdb/client/target_impl.h"
#include "garnet/bin/zxdb/client/thread_impl.h"
#include "garnet/lib/debug_ipc/protocol.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

RemoteAPITest::RemoteAPITest() { loop_.Init(); }

RemoteAPITest::~RemoteAPITest() { loop_.Cleanup(); }

void RemoteAPITest::SetUp() {
  session_ = std::make_unique<Session>(GetRemoteAPIImpl());
}

void RemoteAPITest::TearDown() { session_.reset(); }

Process* RemoteAPITest::InjectProcess(uint64_t process_koid) {
  auto targets = session().system_impl().GetTargetImpls();
  if (targets.size() != 1u) {
    ADD_FAILURE();
    return nullptr;
  }
  if (targets[0]->GetState() != Target::State::kNone) {
    ADD_FAILURE();
    return nullptr;
  }
  targets[0]->CreateProcessForTesting(process_koid, "test");
  return targets[0]->GetProcess();
}

Thread* RemoteAPITest::InjectThread(uint64_t process_koid,
                                    uint64_t thread_koid) {
  debug_ipc::NotifyThread notify;
  notify.process_koid = process_koid;
  notify.record.koid = thread_koid;
  notify.record.name = fxl::StringPrintf("test %" PRIu64, thread_koid);
  notify.record.state = debug_ipc::ThreadRecord::State::kRunning;

  session_->DispatchNotifyThread(
      debug_ipc::MsgHeader::Type::kNotifyThreadStarting, notify);

  return session_->ThreadImplFromKoid(process_koid, thread_koid);
}

void RemoteAPITest::InjectException(
    const debug_ipc::NotifyException& exception) {
  session_->DispatchNotifyException(exception);
}

}  // namespace zxdb
