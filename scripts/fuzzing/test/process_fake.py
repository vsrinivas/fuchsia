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

    def __init__(self, cli, args, **kwargs):
        self._cli = cli
        self._popen = FakeProcess.Popen(cli)
        super(FakeProcess, self).__init__(args)

    @property
    def duration(self):
        return self._popen._duration

    @duration.setter
    def duration(self, duration):
        self._popen._duration = float(duration)

    @property
    def succeeds(self):
        return self._popen._succeeds

    @succeeds.setter
    def succeeds(self, succeeds):
        self._popen._succeeds = succeeds

    @property
    def inputs(self):
        self._popen._stdin.seek(0)
        return self._popen._stdin.read().split('\n')

    def schedule(self, output, start=None, end=None):
        """Sets the output and/or error to be returned later.

        The output will appear in the stdout of the process between the start
        and end times. If start is None, it behaves as if start is now. If end
        is None, the output is not removed once it is added.
        """
        if not start:
            start = self._cli.elapsed
        self._popen._outputs.append((output, start, end))

    def clear(self):
        self._popen._outputs = []

    def popen(self):
        self._popen._start()
        return self._popen

    class Popen(object):
        """Fakes subprocess.Popen for FakeProcess.

        Unlike a real subprocess.Popen, this object always buffers stdio.
        """

        def __init__(self, cli):
            self._cli = cli
            self._duration = 0.0
            self._completion = None
            self._succeeds = True
            self._returncode = None

            self._outputs = []
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

        def _start(self):
            assert not self._completion, 'popen() called twice'
            self._completion = self._cli.elapsed + self._duration
            self._returncode = None
            self._stdin.truncate(0)

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
            """Like subprocess.Popen.poll().

            This method will update the object's stdout and stderr according to
            the schedule and the elapsed time. It will set the returncode once
            its specified duration has elapsed.
            """
            now = self._cli.elapsed
            if not self.returncode and self._completion <= now:
                self._stdout.truncate(0)
                for output, start, end in self._outputs:
                    if now < start:
                        continue
                    if end and end <= now:
                        continue
                    self._stdout.write(output)
                    self._stdout.write('\n')
                self._stdout.flush()
                self._returncode = 0 if self._succeeds else 1
                self._completion = None
            return self.returncode

        def wait(self):
            if self._completion:
                self._cli.sleep(self._completion - self._cli.elapsed)
            return self.poll()

        def kill(self):
            self._completion = None
