# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""A minimal ANSI escape code library."""

import sys

# Reset terminal colour.
_RESET = '\033[0;0m'


# Wrap the string "s" with the given code and a reset prefix.
#
# Returns the string unmodified if stdout is not attached to a terminal.
def wrap(code: str, s: str) -> str:
  if not sys.stdout.isatty():
    # Being piped.
    return s
  return '%s%s%s' % (code, s, _RESET)


# Emit a Select Graphic Rendition (SGR) ANSI code.
def sgr(*codes: int) -> str:
  return '\033[%sm' % (';'.join(str(c) for c in codes))


# Wrap the string with a color.
def red(s: str) -> str:
  return wrap(sgr(31), s)
def green(s: str) -> str:
  return wrap(sgr(32), s)
def yellow(s: str) -> str:
  return wrap(sgr(33), s)
def gray(s: str) -> str:
  return wrap(sgr(90), s)
def bright_green(s: str) -> str:
  return wrap(sgr(1, 32), s)
