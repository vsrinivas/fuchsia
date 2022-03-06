// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_ARTIFACT_H_
#define SRC_SYS_FUZZING_COMMON_ARTIFACT_H_

#include <fuchsia/fuzzer/cpp/fidl.h>

#include <tuple>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/result.h"

namespace fuzzing {

// An |Artifact| is a |FuzzResult| and the associated |Input| that caused it.
class Artifact final {
 public:
  Artifact() = default;
  explicit Artifact(std::tuple<FuzzResult, Input>&& artifact);
  Artifact(FuzzResult fuzz_result, Input&& input);
  Artifact(Artifact&& other) noexcept;
  ~Artifact() = default;

  Artifact& operator=(Artifact&& other) noexcept;
  bool operator==(const Artifact& other) const;
  bool operator!=(const Artifact& other) const;

  FuzzResult fuzz_result() const { return fuzz_result_; }
  const Input& input() const { return input_; }

  Artifact Duplicate() const;
  Input take_input() { return std::move(input_); }

  std::tuple<FuzzResult, Input> take_tuple();

 private:
  FuzzResult fuzz_result_ = FuzzResult::NO_ERRORS;
  Input input_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Artifact);
};

// A |FidlArtifact| is an alias of a |FuzzResult| and an associated |FidlInput|. It is analogous to
// an |Artifact| that can be transferred over a FIDL channel.
using FidlArtifact = std::tuple<FuzzResult, FidlInput>;
inline FidlArtifact MakeFidlArtifact(FuzzResult fuzz_result, FidlInput&& fidl_input) {
  return std::make_tuple(fuzz_result, std::move(fidl_input));
}

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_ARTIFACT_H_
