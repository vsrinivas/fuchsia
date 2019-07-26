// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-compute/hermetic-engine.h>

using A = std::tuple<int, std::tuple<>, int, std::tuple<int>>;
using B = std::array<std::pair<int, std::tuple<int, int>>, 3>;

struct TestEngine : public HermeticComputeEngine<TestEngine, A, B> {
  int64_t operator()(const A& x, const B& y) {
    return (std::get<0>(x) + std::get<2>(x) + std::get<0>(std::get<3>(x)) + y[0].first +
            std::get<0>(y[0].second) + std::get<1>(y[0].second) + y[1].first +
            std::get<0>(y[1].second) + std::get<1>(y[1].second) + y[2].first +
            std::get<0>(y[2].second) + std::get<1>(y[2].second));
  }
};
