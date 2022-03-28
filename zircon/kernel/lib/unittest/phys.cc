// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <stdio.h>
#include <string.h>

#include <ktl/algorithm.h>
#include <ktl/span.h>

#include <ktl/enforce.h>

namespace {

constexpr auto namelen = [](auto&& testcase) { return strlen(testcase.name); };

size_t max_case_namelen(ktl::span<const test_case_element> cases) {
  auto is_shorter = [](auto&& a, auto&& b) { return namelen(a) < namelen(b); };
  auto longest = ktl::max_element(cases.begin(), cases.end(), is_shorter);
  return namelen(*longest);
}

}  // namespace

bool unittest_testcase(const char* name, const test_case_element* cases, size_t n) {
  const size_t max_namelen = n == 0 ? 0 : max_case_namelen({cases, n});

  printf("%s : Running %zu test%s...\n", name, n, n == 1 ? "" : "s");

  size_t passed = 0;
  for (size_t i = 0; i < n; ++i) {
    printf("  %-*s : ", static_cast<int>(max_namelen), cases[i].name);
    bool good = cases[i].fn();
    if (good) {
      passed++;
      printf("PASSED\n");
    } else {
      printf("\n  %-*s : FAILED\n", static_cast<int>(max_namelen), cases[i].name);
    }
  }

  printf("%s : %sll tests passed (%zu/%zu)\n\n", name, passed != n ? "Not a" : "A", passed, n);

  return passed == n;
}
