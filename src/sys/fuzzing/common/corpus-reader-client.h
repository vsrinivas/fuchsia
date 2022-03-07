// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_CORPUS_READER_CLIENT_H_
#define SRC_SYS_FUZZING_COMMON_CORPUS_READER_CLIENT_H_

#include <fuchsia/fuzzer/cpp/fidl.h>

#include <vector>

#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/input.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CorpusReader;

class CorpusReaderClient final {
 public:
  explicit CorpusReaderClient(ExecutorPtr executor);
  ~CorpusReaderClient() = default;

  // FIDL binding methods.
  fidl::InterfaceRequest<CorpusReader> NewRequest();
  void Bind(fidl::InterfaceHandle<CorpusReader> handle);

  // Schedules a sequence of calls |fuchsia.fuzzer.CorpusReader.Next| for each element in the list
  // of |inputs|. The returned promise only completes after all |inputs| have been sent.
  ZxPromise<> Send(std::vector<Input>&& inputs);

 private:
  fidl::InterfacePtr<CorpusReader> ptr_;
  ExecutorPtr executor_;
  Scope scope_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_CORPUS_READER_CLIENT_H_
