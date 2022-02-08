#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import subprocess
from io import StringIO

import test_env
from lib.process import Process


class FakeProcess(Process):
    """A fake for process creation and execution.

       Instead of actually running subprocesses, this class just records
       commands. Other fakes can additionally add canned responses.
    """

    def __init__(self, host, args, **kwargs):
        self._host = host
        self._popen = FakeProcess.Popen(host)

        # Special case to emulate JSON output for `ffx` commands. We can't just
        # do type-based detection because of the empty-output case.
        if ('--machine', 'json') in zip(args[:-1], args[1:]):
            self._popen._output_json = True

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
        return self._popen._stdin.getvalue().split('\n')

    def schedule(self, output, returncode=None, start=None, end=None):
        """Sets the output and/or error to be returned later.

        Between the start and end times, the output will appear in the process's
        stdout, and any specified returncode will override the default return
        code. If start is None, it behaves as if start is now. If end
        is None, the output is not removed once it is added.
        """
        if not start:
            start = self._host.elapsed
        self._popen._outputs.append((output, returncode, start, end))

    def clear(self):
        self._popen._outputs = []

    def popen(self):
        self._popen._start()
        return self._popen

    class Popen(object):
        """Fakes subprocess.Popen for FakeProcess.

        Unlike a real subprocess.Popen, this object always buffers stdio.
        """

        def __init__(self, host):
            self._host = host
            self._duration = 0.0
            self._completion = None
            self._succeeds = True
            self._returncode = None

            self._outputs = []
            self._output_json = False
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
            assert self._completion is None, 'popen() called twice'
            self._completion = self._host.elapsed + self._duration
            self._returncode = None
            self._stdin.truncate(0)
            self._stdin.seek(0)

        def communicate(self, inputs=''):
            """Like subprocess.Popen.communicate().

            In particular, writes bytes from inputs to stdin, and returns a
            tuple of stdout and stderr.
            """
            self._stdin.write(str(inputs))
            self.wait()
            return (self._stdout.getvalue(), self._stderr.getvalue())

        def poll(self):
            """Like subprocess.Popen.poll().

            This method will update the object's stdout and stderr according to
            the schedule and the elapsed time. It will set the returncode once
            its specified duration has elapsed, using the most recently-added
            return code set for the current time range, falling back to the
            `succeeds` property if none was specified.
            """

            now = self._host.elapsed
            if self.returncode is None and self._completion <= now:
                self._stdout.truncate(0)
                self._stdout.seek(0)
                current_outputs = []
                for output, returncode, start, end in self._outputs:
                    if now < start:
                        continue
                    if end and end <= now:
                        continue
                    self._returncode = returncode
                    current_outputs.append(output)

                if self._output_json:
                    self._stdout.write(json.dumps(current_outputs))
                else:
                    self._stdout.write('\n'.join(current_outputs))
                self._stdout.write('\n')
                self._stdout.flush()
                # If no return code was explicitly specified for this time
                # period, fall back to the `succeeds` setting
                if self._returncode is None:
                    self._returncode = 0 if self._succeeds else 1
                self._completion = None
            return self.returncode

        def wait(self):
            if self._completion is not None:
                self._host.sleep(self._completion - self._host.elapsed)
            return self.poll()

        def kill(self):
            self._completion = None
