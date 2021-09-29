// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_CORPUS_READER_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_CORPUS_READER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sync/completion.h>

#include <memory>
#include <mutex>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/binding.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/transceiver.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CorpusReader;

// This is a fake implementation of |fuchsia.fuzzer.CorpusReader|. It accepts corpus inputs pushed
// from the engine and adds them to a queue that can be waited on.
class FakeCorpusReader final : public CorpusReader {
 public:
  FakeCorpusReader();
  ~FakeCorpusReader() override = default;

  // FIDL methods.
  fidl::InterfaceHandle<CorpusReader> NewBinding(async_dispatcher_t* dispatcher);
  void Next(FidlInput fidl_input, NextCallback callback) override;

  // Blocks until a call to |GetNext| would succeed, in which case it returns true, or until the
  // channel is closed, in which case it returns false.
  bool AwaitNext();

  // Returns the next input as submitted by |Next|. This should only be called after |AwaitNext| has
  // returned true.
  Input GetNext();

 private:
  Binding<CorpusReader> binding_;
  std::shared_ptr<Transceiver> transceiver_;
  sync_completion_t sync_;
  std::mutex mutex_;
  std::deque<Input> inputs_ FXL_GUARDED_BY(mutex_);
  bool closed_ FXL_GUARDED_BY(mutex_) = false;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeCorpusReader);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_CORPUS_READER_H_
