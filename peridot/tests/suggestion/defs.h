// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_SUGGESTION_DEFS_H_
#define PERIDOT_TESTS_SUGGESTION_DEFS_H_

namespace {

// Package URLs of the test components used here.
constexpr char kSuggestionTestModule[] =
    "fuchsia-pkg://fuchsia.com/suggestion_test_module#meta/"
    "suggestion_test_module.cmx";

constexpr char kSuggestionTestAction[] = "suggestion_test_action";

constexpr char kSuggestionTestModuleDone[] = "suggestion_test_module_done";

constexpr char kProposalId[] =
    "file:///system/bin/modular_tests/suggestion_proposal_test#proposal";

}  // namespace

#endif  // PERIDOT_TESTS_SUGGESTION_DEFS_H_
