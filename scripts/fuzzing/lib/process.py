#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import subprocess


class Process(object):
    """Represents a child process.

       This class is intentionally similar to subprocess, except that it allows
       various fields to be set before executing the process. Additionally, it
       allows tests to overload process creation and execution in one place;
       see MockProcess.
    """

    def __init__(self, args, **kwargs):
        self.args = args
        self.cwd = kwargs.get('cwd', None)
        self.stdin = kwargs.get('stdin', None)
        self.stdout = kwargs.get('stdout', None)
        self.stderr = kwargs.get('stderr', None)

    def popen(self):
        """Analogous to subprocess.Popen."""
        p = subprocess.Popen(
            self.args,
            cwd=self.cwd,
            stdin=self.stdin,
            stdout=self.stdout,
            stderr=self.stderr)
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
        cmd = self.args
        rc = self.call()
        if rc != 0:
            raise subprocess.CalledProcessError(rc, cmd)
        return rc

    def check_output(self):
        """Analogous to subprocess.check_output."""
        cmd = self.args
        self.stdout = subprocess.PIPE
        p = self.popen()
        out, _ = p.communicate()
        rc = p.returncode
        if rc != 0:
            raise subprocess.CalledProcessError(rc, cmd, output=out)
        return out
