// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "affine_transform_test_utils.h"

std::ostream &
operator<<(std::ostream & os, const affine_transform_t & t)
{
  os << "[sx:" << t.sx;
  if (t.shx)
    os << ",shx:" << t.shx;
  if (t.shy)
    os << ",shy:" << t.shy;
  os << ",sy:" << t.sy;
  if (t.tx || t.ty)
    os << ",tx:" << t.tx << ",ty:" << t.ty;
  os << "]";
  return os;
}

::testing::AssertionResult
AssertAffineTransformEqual(const char *             m_expr,
                           const char *             n_expr,
                           const affine_transform_t m,
                           const affine_transform_t n)
{
  // NOTE: This checks for strict equality, which isn't always very useful for
  //       floating point values. It would be nice to have a float-near check
  //       instead, but doing this is incredibly hard [1], and GoogleTest does
  //       not expose its algorithms for near-float comparisons at the moment.
  //
  //   [1] https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/
  //
  if (m.sx != n.sx || m.shx != n.shx || m.tx != n.tx || m.shy != n.shy || m.sy != n.sy ||
      m.ty != n.ty)
    {
      return ::testing::AssertionFailure()
             << m_expr << " and " << n_expr << " are not equal: " << m << " vs " << n;
    }
  return ::testing::AssertionSuccess();
}
