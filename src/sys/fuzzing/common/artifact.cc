// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/artifact.h"

namespace fuzzing {

Artifact::Artifact(std::tuple<FuzzResult, Input>&& artifact) {
  std::tie(fuzz_result_, input_) = std::move(artifact);
}

Artifact::Artifact(FuzzResult fuzz_result, Input&& input)
    : fuzz_result_(fuzz_result), input_(std::move(input)) {}

Artifact::Artifact(Artifact&& other) noexcept { *this = std::move(other); }

Artifact& Artifact::operator=(Artifact&& other) noexcept {
  fuzz_result_ = other.fuzz_result_;
  other.fuzz_result_ = FuzzResult::NO_ERRORS;
  input_ = std::move(other.input_);
  return *this;
}

bool Artifact::operator==(const Artifact& other) const {
  return fuzz_result_ == other.fuzz_result_ && input_ == other.input_;
}

bool Artifact::operator!=(const Artifact& other) const { return !(*this == other); }

Artifact Artifact::Duplicate() const { return Artifact(fuzz_result_, input_.Duplicate()); }

std::tuple<FuzzResult, Input> Artifact::take_tuple() {
  auto fuzz_result = fuzz_result_;
  fuzz_result_ = FuzzResult::NO_ERRORS;
  return std::make_tuple<FuzzResult, Input>(std::move(fuzz_result), std::move(input_));
}

}  // namespace fuzzing
