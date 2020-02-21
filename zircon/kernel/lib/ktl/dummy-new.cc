// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <new>

// std::stable_sort (used as ktl::stable_sort) uses std::get_temporary_buffer,
// which uses operator new to request a temporary buffer but then works fine
// if it can't get one.  There is no "default" memory allocation in the
// kernel, so just provide a dummy operator new that always fails so
// std::stable_sort falls back to its no-allocation algorithm.

namespace std {

const nothrow_t nothrow;

}  // namespace std

void* operator new(std::size_t, const std::nothrow_t&) noexcept { return nullptr; }
