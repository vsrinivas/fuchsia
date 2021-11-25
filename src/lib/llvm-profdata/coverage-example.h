// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_LLVM_PROFDATA_COVERAGE_EXAMPLE_H_
#define SRC_LIB_LLVM_PROFDATA_COVERAGE_EXAMPLE_H_

void LinkTimeDeadFunction();
void RunTimeDeadFunction();
void MaybeCallRunTimeDeadFunction(bool doit);
void RunTimeCoveredFunction();

#endif  // SRC_LIB_LLVM_PROFDATA_COVERAGE_EXAMPLE_H_
