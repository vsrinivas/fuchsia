#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import errno
import fnmatch
import subprocess
from StringIO import StringIO

import test_env
from lib.cli import CommandLineInterface
from process_fake import FakeProcess


class FakeCLI(CommandLineInterface):
    """Fake command line interface that avoids interacting with the system.

       Attributes:
         history:       List of commands whose execution has been faked.
         log:           Saves messages printed by echo, error, or choose.
         selection:     Pre-selected option for a future call to choose().
    """

    ENVVARS = {'FUCHSIA_DIR': 'fuchsia_dir'}

    def __init__(self):
        super(FakeCLI, self).__init__()
        self._log = []
        self._selection = None
        self._dirs = set()
        self._files = {}
        self._links = {}
        self._envvars = {}
        self._processes = {}
        self._elapsed = 0.0

    @property
    def processes(self):
        """Commands whose creation and/or execution has been faked."""
        return self._processes

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
        assert self._selection, 'Unexpected call to choose()'
        selection = self._selection
        self._selection = None
        return selection

    @selection.setter
    def selection(self, selection):
        assert not self._selection, 'Missing call to choose()'
        self._selection = selection

    @property
    def elapsed(self):
        return self._elapsed

    # Fake I/O routines

    def echo(self, *args, **kwargs):
        for line in args:
            self._log += line.split('\n')

    def choose(self, prompt, choices):
        choice = choices[self.selection - 1]
        return super(FakeCLI, self).choose(prompt, choices, choice)

    # Fake filesystem routines

    def _dereference(self, pathname):
        while pathname in self._links:
            pathname = self._links[pathname]
        return pathname

    def isdir(self, pathname):
        """Fake implmentation overriding Host.isdir."""
        return self._dereference(pathname) in self._dirs

    def isfile(self, pathname):
        """Fake implmentation overriding Host.isfile."""
        return self._dereference(pathname) in self._files

    def glob(self, pattern):
        """Fake implementation overriding Host.glob."""
        return sorted(fnmatch.filter(self._files, pattern))

    class File(StringIO):
        """A file-like object that can be used in "with" statements."""

        def __init__(self):
            self._contents = None

        def open(self, mode):
            """Recreates the StringIO base object to simulate opening."""
            StringIO.__init__(self)
            if mode == 'w' or mode == 'w+':
                return
            self.write(self._contents)
            if mode == 'r' or mode == 'r+':
                self.seek(0, 0)

        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc_value, exc_traceback):
            if not exc_type:
                self.seek(0, 0)
                self._contents = self.read()

    def open(self, pathname, mode='r', on_error=None, missing_ok=False):
        """Opens a fake file for reading and/or writing."""
        pathname = self._dereference(pathname)
        assert pathname not in self._dirs, 'Directory exists: {}'.format(
            pathname)

        if pathname in self._files:
            file = self._files[pathname]
        elif mode != 'r' and mode != 'r+':
            file = FakeCLI.File()
            self._files[pathname] = file
        elif missing_ok:
            return None
        elif on_error:
            self.error(*on_error)
        else:
            self.error('Failed to open {}.'.format(pathname))

        file.open(mode)
        return file

    def mkdir(self, pathname):
        """Fake implementation overriding Host.mkdir."""
        pathname = self._dereference(pathname)
        assert pathname not in self._files, 'File exists: {}'.format(pathname)
        self._dirs.add(pathname)
        self.create_process(['mkdir', '-p', pathname])

    def link(self, pathname, linkname):
        """Fake implementation overriding Host.link."""
        self._links[linkname] = pathname
        self.create_process(['ln', '-s', pathname, linkname])

    def remove(self, pathname):
        pathname = self._dereference(pathname)
        if pathname in self._dirs:
            self._dirs.remove(pathname)
        elif pathname in self._files:
            del self._files[pathname]
        self.create_process(['rm', '-rf', pathname])

    def _mkdtemp(self):
        temp_dir = 'temp_dir'
        self.mkdir(temp_dir)
        return temp_dir

    # Other routines

    def getenv(self, name):
        """Fake of implementation of getenv."""
        if name in self._envvars:
            return self._envvars[name]
        else:
            return FakeCLI.ENVVARS.get(name, None)

    def setenv(self, name, value):
        """Fake of implementation of setenv."""
        self._envvars[name] = value

    def create_process(self, args, **kwargs):
        """Creates a FakeProcess from subprocess-like parameters.

        This method caches the returned object in self._processes, and will
        return the same object when given the same parameters. This allows tests
        to "pre-create" fake processes and set their responses before invoking
        methods under test that create them and use those responses.
        """
        cmd = ' '.join(args)
        process = self._processes.get(cmd, None)
        if not process:
            process = FakeProcess(self, args, **kwargs)
            self._processes[cmd] = process
        return process

    def sleep(self, duration):
        if duration > 0:
            self._elapsed += duration
