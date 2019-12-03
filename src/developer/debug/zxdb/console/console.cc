// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/console.h"

#include "src/developer/debug/zxdb/console/async_output_buffer.h"
#include "src/developer/debug/zxdb/console/command_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace zxdb {

Console* Console::singleton_ = nullptr;

Console::Console(Session* session) : context_(session), weak_factory_(this) {
  FXL_DCHECK(!singleton_);
  singleton_ = this;
}

Console::~Console() {
  FXL_DCHECK(singleton_ == this);
  singleton_ = nullptr;

  // Clear backpointers bound with the callbacks for any pending async buffers.
  for (auto& pair : async_output_)
    pair.second->SetCompletionCallback({});
}

fxl::WeakPtr<Console> Console::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

void Console::Output(const std::string& s) {
  OutputBuffer buffer;
  buffer.Append(s);
  Output(buffer);
}

void Console::Output(const Err& err) {
  OutputBuffer buffer;
  buffer.Append(err);
  Output(buffer);
}

void Console::Output(fxl::RefPtr<AsyncOutputBuffer> output) {
  if (output->is_complete()) {
    // Synchronously available.
    Output(output->DestructiveFlatten());
  } else {
    // Listen for completion.
    //
    // Binds |this|. On our destruction we'll clear all the callbacks to prevent dangling pointers
    // for anything not completed yet.
    AsyncOutputBuffer* output_ptr = output.get();
    output->SetCompletionCallback([this, output_ptr]() {
      Output(output_ptr->DestructiveFlatten());

      auto found = async_output_.find(output_ptr);
      FXL_DCHECK(found != async_output_.end());

      // Clear the completion callback out of paranoia.
      async_output_.erase(found);
    });
    async_output_[output_ptr] = std::move(output);
  }
}

}  // namespace zxdb
