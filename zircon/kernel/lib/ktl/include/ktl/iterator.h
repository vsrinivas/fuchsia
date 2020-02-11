// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ITERATOR_H_
#define ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ITERATOR_H_

#include <iterator>

namespace ktl {

// This includes only the "iterator operations" and "range access" subsets
// of the <iterator> API.  These are useful for treating all sorts of
// container types, raw arrays, pointers, and {...} literal lists uniformly.
// It leaves out iterator_traits and the "iterator adaptors" subset, which
// have not yet seemed desirable to employ in kernel code.

using std::advance;
using std::begin;
using std::cbegin;
using std::cend;
using std::crbegin;
using std::crend;
using std::data;
using std::distance;
using std::empty;
using std::end;
using std::next;
using std::prev;
using std::rbegin;
using std::rend;
using std::size;

}  // namespace ktl

#endif  // ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ITERATOR_H_
