#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys


class CommandLineInterface(object):
    """Represent the command line interface to the host system."""

    def echo(self, *lines):
        """Print an informational message from a list of strings."""
        for line in lines:
            print(line)

    def error(self, *lines):
        """Print an error message and exit."""
        self.echo(*lines)
        sys.exit(1)

    def choose(self, prompt, choices):
        """Displays a simple interactive menu."""
        print(prompt)
        for i, choice in enumerate(choices, 1):
            print("  {}) {}".format(i, choice))

        prompt = "Choose 1-{}: ".format(len(choices))
        choice = None
        while not choice:
            try:
                choice = choices[int(raw_input(prompt)) - 1]
            except ValueError, IndexError:
                print("Invalid selection.")
        return choice
