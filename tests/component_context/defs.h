// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_COMPONENT_CONTEXT_DEFS_H_
#define PERIDOT_TESTS_COMPONENT_CONTEXT_DEFS_H_

namespace {

// Package URLs of the test components used here.
constexpr char kOneAgentUrl[] = "component_context_test_one_agent";
constexpr char kUnstoppableAgent[] = "component_context_test_unstoppable_agent";
constexpr char kTwoAgentUrl[] = "component_context_test_two_agent";

constexpr int kTotalSimultaneousTests = 2;

}  // namespace

#endif  // PERIDOT_TESTS_COMPONENT_CONTEXT_DEFS_H_
