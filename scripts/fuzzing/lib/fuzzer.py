#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse

from host import Host


class Fuzzer(object):
  """Represents a Fuchsia fuzz target.

    This represents a binary fuzz target produced the Fuchsia build, referenced
    by a component
    manifest, and included in a fuzz package.  It provides an interface for
    running the fuzzer in
    different common modes, allowing specific command line arguments to
    libFuzzer to be abstracted.

    Attributes:
      device: A Device where this fuzzer can be run
      host: The build host that built the fuzzer
      pkg: The GN fuzz_package name
      tgt: The GN fuzz_target name
  """

  class NameError(ValueError):
    """Indicates a supplied name is malformed or unusable."""
    pass

  @classmethod
  def make_parser(cls, description, name_required=True):
    """Registers the command line arguments understood by Fuzzer."""
    parser = argparse.ArgumentParser(
        description=description, conflict_handler='resolve')
    parser.add_argument(
        '-n',
        '--name',
        action='store',
        required=name_required,
        help='Fuzzer name to match.  This can be part of the package and/or' +
        ' target name, e.g. "foo", "bar", and "foo/bar" all match' +
        ' "foo_package/bar_target".')
    return parser

  @classmethod
  def filter(cls, fuzzers, name):
    """Filters a list of fuzzer names.

      Takes a list of fuzzer names in the form `pkg`/`tgt` and a name to filter
      on.  If the name is of the form 'x/y', the filtered list will include all
      the fuzzer names where x is a substring of `pkg` and y is a substring of
      `tgt`; otherwise it includes all the fuzzer names where `name` is a
      substring of either `pkg` or `tgt`.

      Returns:
        A list of fuzzer names matching the given name.

      Raises:
        FuzzerNameError: Name is malformed, e.g. of the form 'x/y/z'.
    """
    if not name or name == '':
      return fuzzers
    names = name.split('/')
    if len(names) == 1:
      return list(
          set(Fuzzer.filter(fuzzers, '/' + name))
          | set(Fuzzer.filter(fuzzers, name + '/')))
    elif len(names) != 2:
      raise Fuzzer.NameError('Malformed fuzzer name: ' + name)
    filtered = []
    for pkg, tgt in fuzzers:
      if names[0] in pkg and names[1] in tgt:
        filtered.append((pkg, tgt))
    return filtered
