// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <ktl/type_traits.h>

namespace {

// Tests for ktl::is_one_of.

// Happy case.
static_assert(ktl::is_one_of<int, char, int, bool>::value);
static_assert(ktl::is_one_of_v<int, char, int, bool>);

// No match because unsigned vs. signed.
static_assert(!ktl::is_one_of<unsigned int, char, int, bool>::value);
static_assert(!ktl::is_one_of_v<unsigned int, char, int, bool>);

// No match because of cv qualifier.
static_assert(!ktl::is_one_of<int, char, const int, bool>::value);
static_assert(!ktl::is_one_of_v<int, char, const int, bool>);

}  // namespace
