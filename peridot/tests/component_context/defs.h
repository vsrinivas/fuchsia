// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_COMPONENT_CONTEXT_DEFS_H_
#define PERIDOT_TESTS_COMPONENT_CONTEXT_DEFS_H_

namespace {

// Package URLs of the test components used here.
constexpr char kOneAgentUrl[] =
    "fuchsia-pkg://fuchsia.com/component_context_test_one_agent#meta/"
    "component_context_test_one_agent.cmx";
constexpr char kUnstoppableAgent[] =
    "fuchsia-pkg://fuchsia.com/component_context_test_unstoppable_agent#meta/"
    "component_context_test_unstoppable_agent.cmx";
constexpr char kTwoAgentUrl[] =
    "fuchsia-pkg://fuchsia.com/component_context_test_two_agent#meta/"
    "component_context_test_two_agent.cmx";

constexpr int kTotalSimultaneousTests = 2;

}  // namespace

#endif  // PERIDOT_TESTS_COMPONENT_CONTEXT_DEFS_H_
