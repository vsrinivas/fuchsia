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


class Host(object):
    """Represent the platform-specific interface to the host system.

    Attributes:
        platform:   The name of the current operating system.
        tracing:    Indicates if additional output is enabled.
    """

    # Convenience file descriptor for silencing subprocess output
    DEVNULL = open(os.devnull, 'w')

    # Used to pass tracing flag to tests in subprocesses.
    TRACE_ENVVAR = 'FX_FUZZ_TRACE'

    def __init__(self):
        self._platform = 'mac-x64' if os.uname()[0] == 'Darwin' else 'linux-x64'
        self._fd_out = sys.stdout
        self._fd_err = sys.stderr
        self._tracing = False

    @property
    def platform(self):
        return self._platform

    @property
    def fd_out(self):
        return self._fd_out

    @fd_out.setter
    def fd_out(self, fd_out):
        self._fd_out = fd_out

    @property
    def fd_err(self):
        return self._fd_err

    @fd_out.setter
    def fd_err(self, fd_err):
        self._fd_err = fd_err

    @property
    def tracing(self):
        """Enables detailed output about action the script is taking."""
        return self._tracing

    @tracing.setter
    def tracing(self, tracing):
        self._tracing = tracing

    # I/O routines

    def trace(self, message):
        """Prints execution details to stdout."""
        if self._tracing:
            print('+ {}'.format(message))

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

    def choose(self, prompt, choices):
        """Displays a simple interactive menu."""
        self.echo(prompt + ' (or enter 0 to cancel):')
        for i, choice in enumerate(choices, 1):
            self.echo("  {}) {}".format(i, choice))
        choice = None
        while not choice:
            self.echo("Choose 1-{}: ".format(len(choices)), end='')
            try:
                raw_choice = int(raw_input())
                if raw_choice == 0:
                    self.echo("User canceled.")
                    sys.exit(0)
                choice = choices[raw_choice - 1]
            except ValueError, IndexError:
                self.echo("Invalid selection.")
        return choice

    # Filesystem routines

    def getcwd(self):
        """Returns the current working directory."""
        return os.getcwd()

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
        self.trace('removing: {}'.format(pathname))
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
            self.trace('sleeping: {}'.format(duration))
            time.sleep(duration)


class _TemporaryDirectory(object):
    """A temporary directory that can be used with "with"."""

    def __init__(self, host):
        self._host = host
        self._pathname = None

    @property
    def pathname(self):
        return self._pathname

    def __enter__(self):
        self._pathname = self._host._mkdtemp()
        return self

    def __exit__(self, exc_type, exc_value, exc_traceback):
        self._host.remove(self._pathname)
