#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import subprocess
from StringIO import StringIO

import test_env
from lib.process import Process


class FakeProcess(Process):
    """A fake for process creation and execution.

       Instead of actually running subprocesses, this class just records
       commands. Other fakes can additionally add canned responses.
    """

    def __init__(self, args, **kwargs):
        self._popen = FakeProcess.Popen()
        super(FakeProcess, self).__init__(args)

    @property
    def succeeds(self):
        return self._popen._succeeds

    @succeeds.setter
    def succeeds(self, succeeds):
        self._popen._succeeds = succeeds

    @property
    def stdin(self):
        return self._popen.stdin

    @stdin.setter
    def stdin(self, stdin):
        pass

    @property
    def stdout(self):
        return self._popen.stdout

    @stdout.setter
    def stdout(self, stdout):
        pass

    @property
    def stderr(self):
        return self._popen.stderr

    @stderr.setter
    def stderr(self, stderr):
        pass

    def popen(self):
        assert not self._popen._running, 'popen() called twice: {}'.format(
            self.args)
        self._popen._running = True
        self.stdin.truncate(0)
        return self._popen

    class Popen(object):
        """Fakes subprocess.Popen for FakeProcess.

        Unlike a real subprocess.Popen, this object always buffers stdio.
        """

        def __init__(self):
            self._running = False
            self._succeeds = True
            self._returncode = None
            self._stdin = StringIO()
            self._stdout = StringIO()
            self._stderr = StringIO()

        @property
        def stdin(self):
            return self._stdin

        @property
        def stdout(self):
            return self._stdout

        @property
        def stderr(self):
            return self._stderr

        @property
        def returncode(self):
            return self._returncode

        def communicate(self, inputs=None):
            """Like subprocess.Popen.communicate().

            In particular, writes bytes from inputs to stdin, and returns a
            tuple of stdout and stderr.
            """
            self._stdin.write(str(inputs))
            self.wait()
            self._stdout.seek(0)
            self._stderr.seek(0)
            return (self._stdout.read(), self._stderr.read())

        def poll(self):
            if self._running:
                self._returncode = 0 if self._succeeds else 1
                self._running = False
            return self._returncode

        def wait(self):
            return self.poll()

        def kill(self):
            self._running = False
