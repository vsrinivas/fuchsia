// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_SPINEL_ASSERT_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_SPINEL_ASSERT_H_

//
//
//

#include "spinel.h"

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
spn_result_to_string(spn_result const result);

spn_result
spn_assert_1(char const * const file,
             int32_t const      line,
             bool const         is_abort,
             spn_result const   result);

spn_result
spn_assert_n(char const * const file,
             int32_t const      line,
             bool const         is_abort,
             spn_result const   result,
             uint32_t const     n,
             spn_result const   expect[]);

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
               sizeof((const spn_result[]){ __VA_ARGS__ }) / sizeof(spn_result),                   \
               (const spn_result[]){ __VA_ARGS__ })

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_SPINEL_ASSERT_H_
