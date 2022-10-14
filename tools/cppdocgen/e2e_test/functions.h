// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_CPPDOCGEN_E2E_TEST_FUNCTIONS_H_
#define TOOLS_CPPDOCGEN_E2E_TEST_FUNCTIONS_H_

#include <string>
#include <vector>

void UndocumentedFunction();

// # Custom title function.
//
// This function has a custom title and two parameters.
double CustomTitleFunction(int a, int b);

/// This documentation uses three slashes and the function has default params with templates.
std::string GetStringFromVectors(const std::vector<int>& vector1 = std::vector<int>(),
                                 std::vector<double, std::allocator<double>>* v2 = {},
                                 int max_count = -1);

#endif  // TOOLS_CPPDOCGEN_E2E_TEST_FUNCTIONS_H_
