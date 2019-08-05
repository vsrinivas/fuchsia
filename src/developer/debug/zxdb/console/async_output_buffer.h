// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ASYNC_OUTPUT_BUFFER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ASYNC_OUTPUT_BUFFER_H_

#include <variant>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class Err;

// This class is for collecting formatted output that might be produced in an asynchronous manner
// across many components.
//
// Formatted text (in the form of an OutputBuffer) can be appended or more AsyncOutputBuffers can be
// appended, building up a tree of output. The various parts of this tree can be filled in
// asynchronously and the toplevel buffer's callback will be issued when everything is marked
// complete.
//
// Usage guidelines for general sanity:
//
//   - The same code is responsible for Complete()ing an AsyncOutputBuffer as for creating it.
//
//   - Don't pass an AsyncOutputBuffer to another function and have the function Complete() it.
//
//   - Functions that need async output should generally return an AsyncOutputBuffer that the
//     function arranges to be Complete() when possible. Callers can append this to other buffers as
//     needed.
//
//   - If a function needs to append to an existing AsyncOutputBuffer, pass by raw pointer and do
//     not have the function Complete() it. If that function needs to append asynchronously do
//     something, it should append a new AsyncOutputBuffer that it will take responsibility for
//     Complete()ing.
//
// Example:
//
//    // Creates a buffer and appends some text to it.
//    auto out = fxl::MakeRefCounted<AsyncOutputBuffer>();
//    out->Append("Hello");
//
//    // Now have somebody asynchronously append to this placeholder in the
//    // middle. This might happen after the next call that append "Goodbye".
//    auto sub = fxl::MakeRefCounted<AsyncOutputBuffer>();
//    // Could also Append(sub) here, the order won't matter.
//    DoSomethingWithACallback(
//        [sub](std::string value) {
//          sub->Append();
//          sub->Complete();
//        });
//    out->Append(sub);
//
//    out->Append("Goodbye");
//    out->Complete();
class AsyncOutputBuffer : public fxl::RefCountedThreadSafe<AsyncOutputBuffer> {
 public:
  using CompletionCallback = fit::callback<void()>;

  ~AsyncOutputBuffer() = default;

  // Setting the completion callback will assert if the buffer is_complete() because in that case it
  // will never be called.
  //
  // This can only be set to a nonempty function once, but it can be set with an empty function to
  // clear it.
  void SetCompletionCallback(CompletionCallback cb);

  // Returns true if the buffer has been marked complete (there will be no more nodes appended to
  // it) and all of the children are also is_complete(). Marking a buffer complete and it having
  // complete children are independent events.
  bool is_complete() { return pending_resolution_ == 0 && marked_complete_; }

  // Mirrors the OutputBuffer API with the addition of being able to append AsyncOutputBuffers.
  void Append(std::string str, TextForegroundColor fg = TextForegroundColor::kDefault,
              TextBackgroundColor bg = TextBackgroundColor::kDefault);
  void Append(fxl::RefPtr<AsyncOutputBuffer> buf);
  void Append(Syntax syntax, std::string str);
  void Append(OutputBuffer buf);
  void Append(const Err& err);

  // Call to mark this output buffer complete. This will issue the callback if there is one
  // registered. See is_complete() for additional discussion.
  //
  // Doing additional appends or making it complete again after this call will trigger a debug
  // assertion.
  void Complete();

  // Helper functions that do Append(...) + Complete() since this is a very common use case.
  void Complete(std::string str, TextForegroundColor fg = TextForegroundColor::kDefault,
                TextBackgroundColor bg = TextBackgroundColor::kDefault);
  void Complete(fxl::RefPtr<AsyncOutputBuffer> buf);
  void Complete(Syntax syntax, std::string str);
  void Complete(OutputBuffer buf);
  void Complete(const Err& err);

  // Once this buffer is_complete(), the spans and any sub-AsyncOutputBuffers can be flattened into
  // one vector.
  //
  // This operation is destructive so can only be called once. This node and all child nodes will be
  // empty after this call.
  OutputBuffer DestructiveFlatten();

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(AsyncOutputBuffer);
  FRIEND_MAKE_REF_COUNTED(AsyncOutputBuffer);

  AsyncOutputBuffer() = default;

  // Called when something happened that could have affected is_complete() to issue the callback.
  void CheckComplete();

  // Recursive callback for DestructiveFlatten() that destructively moves all spans out of this node
  // and its children into the given output buffer.
  void DestructiveCollectNodes(OutputBuffer* out);

  CompletionCallback completion_callback_;

  // Reference count for how many children in nodes_ are uncompleted.
  int pending_resolution_ = 0;

  // Set when Complete() has been called. This does not necessarily mean that all children have been
  // completed (a prerequisite for is_complete().
  bool marked_complete_ = false;

  // This buffer is a sequence of nodes. A node is either a span of text that's synchronously
  // available or an owning reference to another async output buffer that may or may not be filled.
  using Span = OutputBuffer::Span;
  using Ref = fxl::RefPtr<AsyncOutputBuffer>;
  using Node = std::variant<Span, Ref>;
  std::vector<Node> nodes_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_ASYNC_OUTPUT_BUFFER_H_
