#!/usr/bin/env python3.8
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import subprocess


class Process(object):
    """Represents a child process.

       This class is intentionally similar to subprocess, except that it allows
       various fields to be set before executing the process. Additionally, it
       allows tests to overload process creation and execution in one place;
       see FakeProcess.
    """

    def __init__(self, args, **kwargs):
        self.args = args
        self._stdin = kwargs.get('stdin', None)
        self._stdout = kwargs.get('stdout', None)
        self._stderr = kwargs.get('stderr', None)

    @property
    def stdin(self):
        return self._stdin

    @stdin.setter
    def stdin(self, stdin):
        self._stdin = stdin

    @property
    def stdout(self):
        return self._stdout

    @stdout.setter
    def stdout(self, stdout):
        self._stdout = stdout

    @property
    def stderr(self):
        return self._stderr

    @stderr.setter
    def stderr(self, stderr):
        self._stderr = stderr

    def popen(self):
        """Analogous to subprocess.Popen."""
        p = subprocess.Popen(
            self.args,
            stdin=self._stdin,
            stdout=self._stdout,
            stderr=self._stderr,
            text=True)
        return p

    def call(self):
        """Analogous to subprocess.call."""
        p = self.popen()
        try:
            return p.wait()
        except:
            p.kill()
            raise

    def check_call(self):
        """Analogous to subprocess.check_call."""
        rc = self.call()
        if rc != 0:
            cmd = ' '.join(self.args)
            raise subprocess.CalledProcessError(rc, cmd)
        return rc

    def check_output(self):
        """Analogous to subprocess.check_output."""
        self._stdout = subprocess.PIPE
        p = self.popen()
        out, _ = p.communicate()
        rc = p.returncode
        if rc != 0:
            cmd = ' '.join(self.args)
            raise subprocess.CalledProcessError(rc, cmd, output=out)
        return out
