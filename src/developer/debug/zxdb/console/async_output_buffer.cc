// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/async_output_buffer.h"

#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

void AsyncOutputBuffer::SetCompletionCallback(CompletionCallback cb) {
  FXL_DCHECK(!is_complete());

  // Don't clobber with a different callback, but let it be cleared.
  FXL_DCHECK(!cb || !completion_callback_);
  completion_callback_ = std::move(cb);
}

void AsyncOutputBuffer::Append(std::string str, TextForegroundColor fg, TextBackgroundColor bg) {
  FXL_DCHECK(!marked_complete_);
  nodes_.emplace_back(Span(std::move(str), fg, bg));
}

void AsyncOutputBuffer::Append(fxl::RefPtr<AsyncOutputBuffer> buf) {
  FXL_DCHECK(!marked_complete_);

  if (!buf->is_complete()) {
    pending_resolution_++;

    // We're keeping a reference to the appended buffer so we know it's safe
    // to capture |this|. We don't want to use a RefPtr here or it will create
    // a reference cycle.
    buf->SetCompletionCallback([this]() {
      FXL_DCHECK(pending_resolution_ > 0);
      pending_resolution_--;
      CheckComplete();
    });
  }

  nodes_.emplace_back(std::move(buf));
}

void AsyncOutputBuffer::Append(Syntax syntax, std::string str) {
  FXL_DCHECK(!marked_complete_);
  nodes_.emplace_back(Span(syntax, std::move(str)));
}

void AsyncOutputBuffer::Append(OutputBuffer buf) {
  FXL_DCHECK(!marked_complete_);
  for (Span& span : buf.spans())
    nodes_.emplace_back(std::move(span));
}

void AsyncOutputBuffer::Append(const Err& err) {
  FXL_DCHECK(!marked_complete_);
  nodes_.emplace_back(Span(Syntax::kNormal, err.msg()));
}

void AsyncOutputBuffer::Complete() {
  FXL_DCHECK(!marked_complete_);
  marked_complete_ = true;
  CheckComplete();
}

void AsyncOutputBuffer::Complete(std::string str, TextForegroundColor fg, TextBackgroundColor bg) {
  Append(std::move(str), fg, bg);
  Complete();
}

void AsyncOutputBuffer::Complete(fxl::RefPtr<AsyncOutputBuffer> buf) {
  Append(std::move(buf));
  Complete();
}

void AsyncOutputBuffer::Complete(Syntax syntax, std::string str) {
  Append(std::move(str));
  Complete();
}

void AsyncOutputBuffer::Complete(OutputBuffer buf) {
  Append(std::move(buf));
  Complete();
}

void AsyncOutputBuffer::Complete(const Err& err) {
  Append(err);
  Complete();
}

OutputBuffer AsyncOutputBuffer::DestructiveFlatten() {
  FXL_DCHECK(is_complete());

  OutputBuffer out;
  DestructiveCollectNodes(&out);
  return out;
}

void AsyncOutputBuffer::CheckComplete() {
  if (is_complete() && completion_callback_) {
    // Clear the object's callback before issuing it.
    CompletionCallback stack_cb = std::move(completion_callback_);
    stack_cb();
  }
}

void AsyncOutputBuffer::DestructiveCollectNodes(OutputBuffer* out) {
  FXL_DCHECK(is_complete());
  for (auto& node : nodes_) {
    if (std::holds_alternative<Span>(node))
      out->Append(std::move(std::get<Span>(node)));
    else
      std::get<Ref>(node)->DestructiveCollectNodes(out);
  }
}

}  // namespace zxdb
