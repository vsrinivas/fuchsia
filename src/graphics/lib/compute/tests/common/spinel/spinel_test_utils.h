// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SPINEL_TEST_UTILS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SPINEL_TEST_UTILS_H_

#include <gtest/gtest.h>
#include <spinel/spinel_types.h>

#include <cfloat>
#include <iostream>
#include <utility>

// This provides GoogleTest and GoogleMock compatible utilities for Spinel
// data types:
//
//   - GoogleTest-compatible printers.
//   - Default constant values for equality checks.
//   - GoogleTest-compatible predicate functions and macros.
//

//
// spn_path_t
//
extern std::ostream &
operator<<(std::ostream & os, spn_path_t path);

//
// spn_raster_t
//
extern std::ostream &
operator<<(std::ostream & os, spn_raster_t raster);

//
// spn_transform_t
//
extern std::ostream &
operator<<(std::ostream & os, const spn_transform_t & transform);

namespace spinel_constants {
static constexpr spn_transform_t identity_transform = { .sx = 1, .sy = 1 };
}

::testing::AssertionResult
AssertSpnTransformEqual(const char *          m_expr,
                        const char *          n_expr,
                        const spn_transform_t m,
                        const spn_transform_t n);

#define ASSERT_SPN_TRANSFORM_EQ(m, n) ASSERT_PRED_FORMAT2(AssertSpnTransformEqual, (m), (n))
#define EXPECT_SPN_TRANSFORM_EQ(m, n) EXPECT_PRED_FORMAT2(AssertSpnTransformEqual, (m), (n))

#define ASSERT_SPN_TRANSFORM_IS_IDENTITY(m)                                                        \
  ASSERT_SPN_TRANSFORM_EQ(m, ::spinel_constants::identity_transform)

#define EXPECT_SPN_TRANSFORM_IS_IDENTITY(m)                                                        \
  EXPECT_SPN_TRANSFORM_EQ(m, ::spinel_constants::identity_transform)

//
// spn_clip_t
//
extern std::ostream &
operator<<(std::ostream & os, const spn_clip_t & clip);

namespace spinel_constants {
static constexpr spn_clip_t default_clip = { 0., 0., FLT_MAX, FLT_MAX };
}

::testing::AssertionResult
AssertSpnClipEqual(const char *     m_expr,
                   const char *     n_expr,
                   const spn_clip_t m,
                   const spn_clip_t n);

#define ASSERT_SPN_CLIP_EQ(m, n) ASSERT_PRED_FORMAT2(AssertSpnClipEqual, (m), (n))
#define EXPECT_SPN_CLIP_EQ(m, n) EXPECT_PRED_FORMAT2(AssertSpnClipEqual, (m), (n))

//
// spn_txty_t
//
extern std::ostream &
operator<<(std::ostream & os, const spn_txty_t & clip);

namespace spinel_constants {
static constexpr spn_txty_t default_txty = { 0, 0 };
}

// GoogleTest-compatible predicate functions and macros.
::testing::AssertionResult
AssertSpnTxtyEqual(const char *     m_expr,
                   const char *     n_expr,
                   const spn_txty_t m,
                   const spn_txty_t n);

#define ASSERT_SPN_TXTY_EQ(m, n) ASSERT_PRED_FORMAT2(AssertSpnTxtyEqual, (m), (n))
#define EXPECT_SPN_TXTY_EQ(m, n) EXPECT_PRED_FORMAT2(AssertSpnTxtyEqual, (m), (n))

//
//  Styling commands
//
extern std::string
spinelStylingCommandsToString(const spn_styling_cmd_t * begin, const spn_styling_cmd_t * end);

extern std::string
spinelStylingCommandsToString(std::initializer_list<spn_styling_cmd_t> ilist);

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SPINEL_TEST_UTILS_H_
