// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/exception_decoder.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/register.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/lib/fxl/logging.h"

// TODO(fidlcat): Look into this.  Removing the hack that led to this (in
// debug_ipc/helper/message_loop.h) seems to work, except it breaks SDK builds
// on CQ in a way I can't repro locally.
#undef __TA_REQUIRES

#include "tools/fidlcat/lib/interception_workflow.h"

namespace fidlcat {

void ExceptionUse::ExceptionDecoded(ExceptionDecoder* decoder) { decoder->Destroy(); }

void ExceptionUse::DecodingError(const DecoderError& error, ExceptionDecoder* decoder) {
  FXL_LOG(ERROR) << error.message();
  decoder->Destroy();
}

void ExceptionDecoder::Decode() {
  if (thread_->GetStack().has_all_frames()) {
    Display();
  } else {
    thread_->GetStack().SyncFrames([this](const zxdb::Err& /*err*/) { Display(); });
  }
}

void ExceptionDecoder::Display() {
  const zxdb::Stack& stack = thread_->GetStack();
  for (size_t i = stack.size() - 1;; --i) {
    const zxdb::Frame* caller = stack[i];
    caller_locations_.push_back(caller->GetLocation());
    if (i == 0) {
      break;
    }
  }
  use_->ExceptionDecoded(this);
}

void ExceptionDecoder::Destroy() {
  InterceptionWorkflow* workflow = workflow_;
  uint64_t process_id = process_id_;
  dispatcher_->DeleteDecoder(this);
  workflow->ProcessDetached(process_id);
}

void ExceptionDisplay::ExceptionDecoded(ExceptionDecoder* decoder) {
  const Colors& colors = dispatcher_->colors();
  line_header_ = decoder->thread()->GetProcess()->GetName() + ' ' + colors.red +
                 std::to_string(decoder->thread()->GetProcess()->GetKoid()) + colors.reset + ':' +
                 colors.red + std::to_string(decoder->thread_id()) + colors.reset + ' ';

  os_ << '\n';

  // Display caller locations.
  DisplayStackFrame(dispatcher_->colors(), line_header_, decoder->caller_locations(), os_);

  os_ << line_header_ << colors.red << "thread stopped on exception" << colors.reset << '\n';

  // Now our job is done, we can destroy the object.
  decoder->Destroy();
}

void ExceptionDisplay::DecodingError(const DecoderError& error, ExceptionDecoder* decoder) {
  std::string message = error.message();
  size_t pos = 0;
  for (;;) {
    size_t end = message.find('\n', pos);
    const Colors& colors = dispatcher_->colors();
    os_ << decoder->thread()->GetProcess()->GetName() << ' ' << colors.red
        << decoder->thread()->GetProcess()->GetKoid() << colors.reset << ':' << colors.red
        << decoder->thread_id() << colors.reset << ": " << colors.red
        << error.message().substr(pos, end) << colors.reset << '\n';
    if (end == std::string::npos) {
      break;
    }
    pos = end + 1;
  }
  os_ << '\n';
  decoder->Destroy();
}

}  // namespace fidlcat
