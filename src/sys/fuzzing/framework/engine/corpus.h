// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_ENGINE_CORPUS_H_
#define SRC_SYS_FUZZING_FRAMEWORK_ENGINE_CORPUS_H_

#include <stddef.h>

#include <mutex>
#include <random>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {

// Represents a set of fuzzing inputs. All inputs are held in memory, since writing them out to
// "disk" within the test runner framework would only write them to memfs, and lead to the same
// overall memory pressure. A corpus always includes an empty input, and so is never completely
// empty.
class Corpus final {
 public:
  Corpus();
  Corpus(Corpus&& other) noexcept { *this = std::move(other); }
  ~Corpus() = default;

  Corpus& operator=(Corpus&& other) noexcept;

  size_t num_inputs() FXL_LOCKS_EXCLUDED(mutex_);
  size_t total_size() FXL_LOCKS_EXCLUDED(mutex_);

  // Lets this objects add defaults to unspecified options.
  static void AddDefaults(Options* options);

  // Sets options. This will reset the PRNG.
  void Configure(const std::shared_ptr<Options>& options);

  // Adds the input to the corpus. Returns ZX_ERR_BUFFER_TOO_SMALL if the input exceeds the max size
  // specified by the options; ZX_OK otherwise.
  zx_status_t Add(Input input) FXL_LOCKS_EXCLUDED(mutex_);

  // Returns true and the input at |offset| in the corpus via |out| if |offset| is less than the
  // number of inputs; otherwise returns false and sets |out| to an empty input.
  bool At(size_t offset, Input* out) FXL_LOCKS_EXCLUDED(mutex_);

  // Returns a random element from the corpus via |out| This will always succeed, as this method
  // can pick the implicitly included empty element.
  void Pick(Input* out) FXL_LOCKS_EXCLUDED(mutex_);

 private:
  std::shared_ptr<Options> options_;
  std::minstd_rand prng_;
  std::mutex mutex_;

  // TODO(fxbug.dev/84361): Currently, all inputs are held in memory. It may be desirable to store
  // some inputs on local storage when the corpus grows too large.
  std::vector<Input> inputs_ FXL_GUARDED_BY(mutex_);
  size_t total_size_ FXL_GUARDED_BY(mutex_) = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(Corpus);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_CORPUS_H_
