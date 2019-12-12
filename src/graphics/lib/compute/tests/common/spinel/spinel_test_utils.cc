// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spinel_test_utils.h"

#include "spinel/spinel_opcodes.h"
#include "tests/common/list_ostream.h"

//
// spn_path_t
//

std::ostream &
operator<<(std::ostream & os, const spn_path_t path)
{
  os << "SpnPath[";
  if (path.handle == SPN_PATH_INVALID.handle)
    os << "INVALID";
  else
    os << path.handle;
  os << "]";
  return os;
}

//
// spn_raster_t
//

std::ostream &
operator<<(std::ostream & os, const spn_raster_t raster)
{
  os << "SpnRaster[";
  if (raster.handle == SPN_RASTER_INVALID.handle)
    os << "INVALID";
  else
    os << raster.handle;
  os << "]";
  return os;
}

//
// spn_transform_t
//

std::ostream &
operator<<(std::ostream & os, const spn_transform_t & transform)
{
  os << "SpnTransform[sx:" << transform.sx;
  if (transform.shx != 0.)
    os << ",shx:" << transform.shx;
  if (transform.tx != 0.)
    os << ",tx:" << transform.tx;
  if (transform.shy != 0.)
    os << ",shy:" << transform.shy;
  os << ",sy:" << transform.sy;
  if (transform.w0 != 0.)
    os << ",w0:" << transform.w0;
  if (transform.w1 != 0.)
    os << ",w1:" << transform.w1;
  os << "]";
  return os;
}

::testing::AssertionResult
AssertSpnTransformEqual(const char *          m_expr,
                        const char *          n_expr,
                        const spn_transform_t m,
                        const spn_transform_t n)
{
  // NOTE: This checks for strict equality, which isn't always very useful for
  //       floating point values. It would be nice to have a float-near check
  //       instead, but doing this is incredibly hard [1], and GoogleTest does
  //       not expose its algorithms for near-float comparisons at the moment.
  //
  //   [1] https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/
  //
  if (m.sx != n.sx || m.shx != n.shx || m.tx != n.tx || m.shy != n.shy || m.sy != n.sy ||
      m.ty != n.ty || m.w0 != n.w0 || m.w1 != n.w1)
    {
      return ::testing::AssertionFailure()
             << m_expr << " and " << n_expr << " are not equal: " << m << " vs " << n;
    }
  return ::testing::AssertionSuccess();
}

//
// spn_clip_t
//

std::ostream &
operator<<(std::ostream & os, const spn_clip_t & clip)
{
  os << "SpnClip[";
  os << "x0:" << clip.x0 << ",y0:" << clip.y0 << ",x1:" << clip.x1 << ",y1:" << clip.y1;
  os << "]";
  return os;
}

::testing::AssertionResult
AssertSpnClipEqual(const char * m_expr, const char * n_expr, const spn_clip_t m, const spn_clip_t n)
{
  // See comment in AssertSpnTransformEqual about equality comparisons.
  if (m.x0 != n.x0 || m.y0 != n.y0 || m.x1 != n.x1 || m.y1 != n.y1)
    {
      return ::testing::AssertionFailure()
             << m_expr << " and " << n_expr << " are not equal: " << m << " vs " << n;
    }
  return ::testing::AssertionSuccess();
}

//
// spn_txty_t
//

std::ostream &
operator<<(std::ostream & os, const spn_txty_t & txty)
{
  os << "SpnTxty["
     << "tx:" << txty.tx << ",ty:" << txty.ty << "]";
  return os;
}

::testing::AssertionResult
AssertSpnTxtyEqual(const char * m_expr, const char * n_expr, const spn_txty_t m, const spn_txty_t n)
{
  // See comment in AssertSpnTransformEqual about equality comparisons.
  if (m.tx != n.tx || m.ty != n.ty)
    {
      return ::testing::AssertionFailure()
             << m_expr << " and " << n_expr << " are not equal: " << m << " vs " << n;
    }
  return ::testing::AssertionSuccess();
}

//
//  Styling commands
//

// clang-format off

// List of simple commands, i.e. those that do not take arguments from the
// command stream.
#define LIST_SIMPLE_STYLING_OPCODES(macro) \
  macro(NOOP) \
  macro(COVER_NONZERO) \
  macro(COVER_EVENODD) \
  macro(COVER_ACCUMULATE) \
  macro(COVER_MASK) \
  macro(COVER_WIP_ZERO) \
  macro(COVER_ACC_ZERO) \
  macro(COVER_MASK_ZERO) \
  macro(COVER_MASK_ONE) \
  macro(COVER_MASK_INVERT) \
  macro(COLOR_FILL_SOLID) \
  macro(COLOR_FILL_GRADIENT_LINEAR) \
  macro(COLOR_WIP_ZERO) \
  macro(COLOR_ACC_ZERO) \
  macro(BLEND_OVER) \
  macro(BLEND_PLUS) \
  macro(BLEND_MULTIPLY) \
  macro(BLEND_KNOCKOUT) \
  macro(COVER_WIP_MOVE_TO_MASK) \
  macro(COVER_ACC_MOVE_TO_MASK) \
  macro(COLOR_ACC_OVER_BACKGROUND) \
  macro(COLOR_ACC_STORE_TO_SURFACE) \
  macro(COLOR_ACC_TEST_OPACITY) \
  macro(COLOR_ILL_ZERO) \
  macro(COLOR_ILL_COPY_ACC) \
  macro(COLOR_ACC_MULTIPLY_ILL)

// clang-format on

std::string
spinelStylingCommandsToString(const spn_styling_cmd_t * begin, const spn_styling_cmd_t * end)
{
  std::stringstream ss;
  list_ostream      ls(ss);
  for (; begin < end; ++begin)
    {
      spn_styling_cmd_t cmd = *begin;

#define CASE_SIMPLE(name_)                                                                         \
  case SPN_STYLING_OPCODE_##name_:                                                                 \
    ls << #name_;                                                                                  \
    break;

      switch (cmd)
        {
          LIST_SIMPLE_STYLING_OPCODES(CASE_SIMPLE)

          // TODO(digit): Handle commands with parameters correctly!
          default:
            char temp[32];
            snprintf(temp, sizeof(temp), "CMD[%u]", cmd);
            ls << temp;
        }
#undef CASE_SIMPLE
      ls << ls.comma;
    }

  return ss.str();
}

std::string
spinelStylingCommandsToString(std::initializer_list<spn_styling_cmd_t> ilist)
{
  return spinelStylingCommandsToString(ilist.begin(), ilist.end());
}
