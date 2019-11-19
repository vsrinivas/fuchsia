// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include <stdio.h>
#include <stdlib.h>

//
//
//

#include "spinel/spinel_assert.h"

//
//
//

#define SPN_RESULT_TO_STR(result)                                                                  \
  case result:                                                                                     \
    return #result

//
//
//

char const *
spn_result_t_to_string(spn_result_t const result)
{
  switch (result)
    {
#undef SPN_RESULT
#define SPN_RESULT(_result) SPN_RESULT_TO_STR(_result);

      SPN_RESULTS();

      default:
        return "UNKNOWN SPN RESULT";
    }
}

//
//
//

spn_result_t
spn_assert_1(char const * const file,
             int32_t const      line,
             bool const         is_abort,
             spn_result_t const result)
{
  if (result != SPN_SUCCESS)
    {
      char const * const spn_result_t_str = spn_result_t_to_string(result);

      fprintf(stderr,
              "\"%s\", line %d: spn_assert(%d) = \"%s\"\n",
              file,
              line,
              result,
              spn_result_t_str);

      if (is_abort)
        {
          abort();
        }
    }

  return result;
}

//
//
//

spn_result_t
spn_assert_n(char const * const file,
             int32_t const      line,
             bool const         is_abort,
             spn_result_t const result,
             uint32_t const     n,
             spn_result_t const expect[])
{
  bool match = false;

  for (uint32_t ii = 0; ii < n; ii++)
    {
      match = match || (expect[ii] == result);
    }

  if (!match)
    {
      char const * const spn_result_t_str = spn_result_t_to_string(result);

      fprintf(stderr,
              "\"%s\", line %d: spn_assert(%d) = \"%s\"\n",
              file,
              line,
              result,
              spn_result_t_str);

      if (is_abort)
        {
          abort();
        }
    }

  return result;
}

//
//
//
