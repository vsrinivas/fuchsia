// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/exception_decoder.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "tools/fidlcat/lib/interception_workflow.h"

namespace fidlcat {

void ExceptionDecoder::Decode() {
  zxdb::Thread* thread = get_thread();
  if (thread == nullptr) {
    Destroy();
    return;
  }
  if (thread->GetStack().has_all_frames()) {
    Decoded();
  } else {
    thread->GetStack().SyncFrames([this](const zxdb::Err& /*err*/) { Decoded(); });
  }
}

void ExceptionDecoder::Decoded() {
  zxdb::Thread* thread = get_thread();
  if (thread == nullptr) {
    Destroy();
    return;
  }

  std::vector<zxdb::Location> caller_locations;
  const zxdb::Stack& stack = thread->GetStack();
  if (stack.size() > 0) {
    for (size_t i = stack.size() - 1;; --i) {
      const zxdb::Frame* caller = stack[i];
      caller_locations.push_back(caller->GetLocation());
      if (i == 0) {
        break;
      }
    }
  }

  Thread* fidlcat_thread = dispatcher_->SearchThread(thread_id());
  if (fidlcat_thread == nullptr) {
    Process* process = dispatcher_->SearchProcess(process_id());
    if (process == nullptr) {
      process = dispatcher_->CreateProcess(process_name(), process_id(),
                                           thread->GetProcess()->GetWeakPtr());
    }
    fidlcat_thread = dispatcher_->CreateThread(thread_id(), process);
  }
  auto event = std::make_shared<ExceptionEvent>(timestamp(), fidlcat_thread);
  CopyStackFrame(caller_locations, &event->stack_frame());
  dispatcher_->AddExceptionEvent(std::move(event));

  Destroy();
}

void ExceptionDecoder::Destroy() {
  InterceptionWorkflow* workflow = workflow_;
  uint64_t process_id = process_id_;
  uint64_t timestamp = timestamp_;
  dispatcher_->DeleteDecoder(this);
  workflow->ProcessDetached(process_id, timestamp);
}

}  // namespace fidlcat
