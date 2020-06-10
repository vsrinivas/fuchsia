#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import test_env
from lib.cli import CommandLineInterface


class FakeCLI(CommandLineInterface):
    """Fake command line interface that avoids interacting with the system.

       Attributes:
         log:           Saves messages printed by echo, error, or choose.
         selection:     Pre-selected option for a future call to choose().
    """

    def __init__(self):
        self._log = []
        self._selection = None

    @property
    def log(self):
        """Saves messages printed by echo, error, or choose.

           NOTE! Reading this attribute clears it (allowing it to be easily
           reused multiple times in a single test).
        """
        log = self._log
        self._log = []
        return log

    @property
    def selection(self):
        """Pre-selected option for a future call to choose()."""
        if not self._selection:
            raise RuntimeError('Unexpected call to choose()')
        selection = self._selection
        self._selection = None
        return selection

    @selection.setter
    def selection(self, selection):
        if self._selection:
            raise RuntimeError('Missing call to choose()')
        self._selection = selection

    def echo(self, *lines):
        for line in lines:
            self._log += line.split('\n')

    def choose(self, prompt, choices):
        self._log += [prompt]
        choice = choices[self.selection - 1]
        return choice
