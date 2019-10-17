// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_ASSERT_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_ASSERT_H_

//
//
//

#include <stdbool.h>
#include <stdint.h>

#include "spinel_result.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

char const *
spn_result_t_to_string(spn_result_t const result);

spn_result_t
spn_assert_1(char const * const file,
             int32_t const      line,
             bool const         is_abort,
             spn_result_t const result);

spn_result_t
spn_assert_n(char const * const file,
             int32_t const      line,
             bool const         is_abort,
             spn_result_t const result,
             uint32_t const     n,
             spn_result_t const expect[]);

//
//
//

#define spn(...) spn_assert_1(__FILE__, __LINE__, true, (spn_##__VA_ARGS__));

#define spn_ok(_result) spn_assert_1(__FILE__, __LINE__, true, _result);

//
//
//

#define spn_expect(_result, ...)                                                                   \
  spn_assert_n(__FILE__,                                                                           \
               __LINE__,                                                                           \
               true,                                                                               \
               _result,                                                                            \
               sizeof((const spn_result_t[]){ __VA_ARGS__ }) / sizeof(spn_result_t),               \
               (const spn_result_t[]){ __VA_ARGS__ })

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_ASSERT_H_
