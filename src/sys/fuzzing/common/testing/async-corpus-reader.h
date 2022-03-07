// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/92490): Rename this when |Controller| is migrated.
#ifndef SRC_SYS_FUZZING_COMMON_TESTING_ASYNC_CORPUS_READER_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_ASYNC_CORPUS_READER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <stdint.h>

#include <memory>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/input.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CorpusReader;

// This is a fake implementation of |fuchsia.fuzzer.CorpusReader|. It accepts corpus inputs pushed
// from the engine and adds them to a queue that can be waited on.
class FakeAsyncCorpusReader final : public CorpusReader {
 public:
  explicit FakeAsyncCorpusReader(ExecutorPtr executor);
  ~FakeAsyncCorpusReader() override = default;

  const std::vector<Input>& corpus() const { return corpus_; }

  // |Next| will return an error after this many successful calls.
  void set_error_after(int64_t error_after) { error_after_ = error_after; }

  // FIDL methods.
  void Bind(fidl::InterfaceRequest<CorpusReader> request);
  fidl::InterfaceHandle<CorpusReader> NewBinding();
  void Next(FidlInput fidl_input, NextCallback callback) override;

 private:
  fidl::Binding<CorpusReader> binding_;
  ExecutorPtr executor_;
  int64_t error_after_ = -1;
  std::vector<Input> corpus_;
  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeAsyncCorpusReader);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_ASYNC_CORPUS_READER_H_
