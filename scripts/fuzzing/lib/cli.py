#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import errno
import glob
import os
import shutil
import sys
import tempfile
import time

from process import Process


class CommandLineInterface(object):
    """Represent the command line interface to the host system.

       This object can be used directly, or as a context manager. When used as
       the latter, it provides a temporary directory that it automatically
       cleans up on exit.

       Attributes:
         tempdir:   A temporary directory that will be removed on context exit.
    """

    # Convenience file descriptor for silencing subprocess output
    DEVNULL = open(os.devnull, 'w')

    def __init__(self, fd_out=None, fd_err=None, tracing=False):
        self._platform = 'mac-x64' if os.uname()[0] == 'Darwin' else 'linux-x64'
        self._fd_out = fd_out if fd_out else sys.stdout
        self._fd_err = fd_err if fd_err else sys.stdout
        self._tracing = tracing

    @property
    def platform(self):
        return self._platform

    @property
    def tracing(self):
        """Enables detailed output about action the script is taking."""
        return self._tracing

    @tracing.setter
    def tracing(self, tracing):
        self._tracing = tracing

    # I/O routines

    def trace(self, *lines, **kwargs):
        if self._tracing:
            self.echo(['+ {}'.format(line) for line in lines], **kwargs)

    def echo(self, *args, **kwargs):
        """Print an informational message from a list of strings.

        Arguments:
            fd      File descriptor to print to.
            end     Terminating character to append to message.
        """
        if not args:
            args = ['']
        fd = kwargs.pop('fd', self._fd_out)
        end = kwargs.pop('end', '\n')
        assert not kwargs, 'Unexpected keyword arguments: {}'.format(kwargs)
        for line in args:
            fd.write(line)
            fd.write(end)
            fd.flush()

    def error(self, *lines, **kwargs):
        """Print an error message and exit."""
        assert lines, 'Fatal error without error message.'
        fd = kwargs.pop('fd', self._fd_err)
        status = kwargs.pop('status', 1)
        assert not kwargs, 'Unexpected keyword arguments: {}'.format(kwargs)
        self.echo('ERROR: {}'.format(lines[0]), fd=fd)
        for line in lines[1:]:
            self.echo('       {}'.format(line), fd=fd)
        sys.exit(status)

    def choose(self, prompt, choices, preselected=None):
        """Displays a simple interactive menu."""
        self.echo(prompt)
        for i, choice in enumerate(choices, 1):
            self.echo("  {}) {}".format(i, choice))

        prompt = "Choose 1-{}: ".format(len(choices))
        choice = preselected
        while not choice:
            try:
                choice = choices[int(raw_input(prompt)) - 1]
            except ValueError, IndexError:
                self.echo("Invalid selection.")
        return choice

    # Filesystem routines

    def isfile(self, pathname):
        """Returns true for files that exist on the host."""
        return os.path.isfile(pathname)

    def isdir(self, pathname):
        """Returns true for directories that exist on the host."""
        return os.path.isdir(pathname)

    def glob(self, pattern):
        """Returns a list of pathnames from shell-expanding the pattern."""
        return sorted(glob.glob(pattern))

    def open(self, pathname, mode='r', on_error=None, missing_ok=False):
        """Opens and returns a file-like object.

           It is the callers responsibility to clean up, preferably by using a
           "with" statement.

           Arguments:
             mode:          Same as for built-in open()
             on_error:      Message to display if file fails to open.
             missing_ok:    If true, return None if file not found.
        """
        try:
            self.trace(' opening: {}'.format(pathname))
            return open(pathname, mode)
        except IOError as e:
            if e.errno != errno.ENOENT:
                raise
            elif missing_ok:
                return None
            elif on_error:
                self.error(*on_error)
            else:
                self.error('Failed to open {}.'.format(pathname))

    def readfile(self, pathname, **kwargs):
        """Returns the contents of a file."""
        opened = self.open(pathname, **kwargs)
        if not opened:
            return None
        try:
            return opened.read().strip()
        finally:
            opened.close()

    def touch(self, pathname):
        with self.open(pathname, 'a') as opened:
            pass

    def mkdir(self, pathname):
        """Creates a directory and its parents if any of them are missing."""
        self.trace('creating: {}'.format(pathname))
        try:
            os.makedirs(pathname)
        except OSError as e:
            if e.errno != errno.EEXIST:
                raise

    def link(self, pathname, linkname):
        """Creates or replaces a symbolic link from linkname to pathname."""
        self.trace(' linking: {}'.format(linkname))
        self.trace('      to: {}'.format(pathname))
        try:
            os.unlink(linkname)
        except OSError as e:
            if e.errno != errno.ENOENT:
                raise
        os.symlink(pathname, linkname)

    def remove(self, pathname):
        if self.isdir(pathname):
            shutil.rmtree(pathname)
        else:
            os.remove(pathname)

    def _mkdtemp(self):
        return tempfile.mkdtemp()

    def temp_dir(self):
        return _TemporaryDirectory(self)

    # Other routines.

    def getenv(self, name):
        return os.getenv(name)

    def create_process(self, args, **kwargs):
        self.trace(' running: {}'.format(' '.join(args)))
        return Process(args, **kwargs)

    def sleep(self, duration):
        if duration > 0:
            time.sleep(duration)


class _TemporaryDirectory(object):
    """A temporary directory that can be used with "with"."""

    def __init__(self, cli):
        self._cli = cli
        self._pathname = None

    @property
    def pathname(self):
        return self._pathname

    def __enter__(self):
        self._pathname = self._cli._mkdtemp()
        return self

    def __exit__(self, exc_type, exc_value, exc_traceback):
        self._cli.remove(self._pathname)
