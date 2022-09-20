// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/command_context.h"

#include "src/developer/debug/zxdb/common/ref_ptr_to.h"
#include "src/developer/debug/zxdb/console/console.h"

namespace zxdb {

CommandContext::CommandContext(Console* console)
    : weak_console_(console ? console->GetWeakPtr() : nullptr) {}
CommandContext::~CommandContext() = default;

void CommandContext::Output(fxl::RefPtr<AsyncOutputBuffer> output) {
  if (output->is_complete()) {
    // Synchronously available.
    Output(output->DestructiveFlatten());
  } else {
    // Listen for completion.
    AsyncOutputBuffer* output_ptr = output.get();
    output->SetCompletionCallback([this_ref = RefPtrTo(this), output_ptr = output.get()]() {
      this_ref->Output(output_ptr->DestructiveFlatten());

      auto found = this_ref->async_output_.find(output_ptr);
      FX_DCHECK(found != this_ref->async_output_.end());
      this_ref->async_output_.erase(found);
    });
    async_output_[output_ptr] = std::move(output);
  }
}

ConsoleContext* CommandContext::GetConsoleContext() const {
  if (weak_console_)
    return &weak_console_->context();
  return nullptr;
}

// ConsoleCommandContext ---------------------------------------------------------------------------

ConsoleCommandContext::ConsoleCommandContext(Console* console, CompletionCallback done)
    : CommandContext(console), done_(std::move(done)) {}

ConsoleCommandContext::~ConsoleCommandContext() {
  if (done_)
    done_(first_error_);
}

void ConsoleCommandContext::Output(const OutputBuffer& output) {
  if (console())
    console()->Output(output);
}

void ConsoleCommandContext::ReportError(const Err& err) {
  if (!first_error_.has_error())
    first_error_ = err;

  OutputBuffer out;
  out.Append(err);
  Output(out);
}

// OfflineCommandContext ---------------------------------------------------------------------------

OfflineCommandContext::OfflineCommandContext(Console* console, CompletionCallback done)
    : CommandContext(console), done_(std::move(done)) {}

OfflineCommandContext::~OfflineCommandContext() {
  if (done_)
    done_(std::move(outputs_), std::move(errors_));
}

void OfflineCommandContext::Output(const OutputBuffer& output) { outputs_.push_back(output); }

void OfflineCommandContext::ReportError(const Err& err) { errors_.push_back(err); }

}  // namespace zxdb
