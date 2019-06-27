// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_UTIL_TIME_H_
#define GARNET_LIB_UI_GFX_UTIL_TIME_H_

#include <lib/async/default.h>
#include <lib/async/time.h>

namespace scenic_impl {
namespace gfx {

// Obtain the default dispatcher's notion of timestamp "now" in Scenic. This
// function also helps to reduce clutter and boilerplate.
//
// It devolves to zx_clock_get_monotonic() for non-test execution, but uses an
// alternate timebase in test situations, which reduces test flakes.
//
// To get it as zx::time, just wrap the result with zx::time().
//
// If you have a specific dispatcher you'd like to use, then request the time
// directly from that dispatcher. E.g.,
//   zx_time_t now = async_now(dispatcher);
//   zx::time now = async::Now(dispatcher);
inline zx_time_t dispatcher_clock_now() { return async_now(async_get_default_dispatcher()); }

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_UTIL_TIME_H_
