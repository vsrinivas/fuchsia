// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

namespace fbenchmark {

void RegisterTest(const char* name, std::function<void()> func);
int BenchmarksMain(int argc, char** argv, bool run_gbenchmark);

}  // namespace fbenchmark
