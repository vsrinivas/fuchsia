// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_SPINEL_ASSERT_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_SPINEL_ASSERT_H_

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
spinel_result_t_to_string(spinel_result_t const result);

spinel_result_t
spinel_assert_1(char const * const    file,
                int32_t const         line,
                bool const            is_abort,
                spinel_result_t const result);

spinel_result_t
spinel_assert_n(char const * const    file,
                int32_t const         line,
                bool const            is_abort,
                spinel_result_t const result,
                uint32_t const        n,
                spinel_result_t const expect[]);

//
//
//

#define spinel(...) spinel_assert_1(__FILE__, __LINE__, true, (spinel_##__VA_ARGS__));

#define spinel_ok(result_) spinel_assert_1(__FILE__, __LINE__, true, result_);

//
//
//

#define spinel_expect(result_, ...)                                                                \
  spinel_assert_n(__FILE__,                                                                        \
                  __LINE__,                                                                        \
                  true,                                                                            \
                  result_,                                                                         \
                  sizeof((const spinel_result_t[]){ __VA_ARGS__ }) / sizeof(spinel_result_t),      \
                  (const spinel_result_t[]){ __VA_ARGS__ })

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_SPINEL_ASSERT_H_
