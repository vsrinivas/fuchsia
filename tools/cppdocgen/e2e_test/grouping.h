// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_CPPDOCGEN_E2E_TEST_GROUPING_H_
#define TOOLS_CPPDOCGEN_E2E_TEST_GROUPING_H_

// These two are not grouped because there's no heading and the names are not the same.
void UngroupedOne();
void UngroupedTwo();

// Not grouped defines.
#define UNGROUPED_ONE 1
#define UNGROUPED_TWO 2

// These two are grouped because the name is the same, even though there is no explicit heading.
void GroupedImplicitly(int a);
void GroupedImplicitly(double a);

// # Explicitly grouped functions.
//
// These functions have no naming similarities but since there is no blank line nor comment between
// them, and there is a single comment beginning with a heading, they go into their own section.
void GroupedExplicitlyOne(int this_is_a_vary_long_name_that_forces_the_next_line_break,
                          const char* the_line_breaks_before_here);
void GroupedExplicitlyTwo(double a);

// # GROUPED defines
//
// These are the explicitly grouped defines.
#define GROUPED_ONE 1
#define GROUPED_TWO 2

class MyClass {
 public:
  // These two constructors are grouped and this is the comment for them.
  MyClass();
  explicit MyClass(int a);
  // This constructor will go in a separate section due to this separate documentation.
  MyClass(int a, int b);

  int& size();
  const int& size() const;

  // # Iterator functions
  //
  // These functions are explicitly grouped.
  int begin();
  int end();
};

#endif  // TOOLS_CPPDOCGEN_E2E_TEST_GROUPING_H_
