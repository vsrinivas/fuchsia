# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Description:
#   A variation of select() that prints a meaningful error message if
#   --config=fuchsia is absent. This avoid cryptic build errors about
#   missing attribute values.

_ERROR = """
***********************************************************
* You have to specify a config in order to build Fuchsia. *
*                                                         *
* For example: --config=fuchsia.                          *
***********************************************************
"""

def fuchsia_select(configs):
  """ select() variant that prints a meaningful error.

  Args:
    config: A dict of config name-value pairs.

  Returns:
    Selected attribute value depending on the config.
  """
  return select(configs, no_match_error = _ERROR)
