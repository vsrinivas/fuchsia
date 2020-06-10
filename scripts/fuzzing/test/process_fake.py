#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import test_env
from lib.process import Process


class FakeProcess(Process):
    """A fake for process creation and execution.

       Instead of actually running subprocesses, this class just records
       commands. Other fakes can additionally add canned responses.
    """

    def __init__(self, host, args, **kwargs):
        self.host = host
        self.response = ''
        super(FakeProcess, self).__init__(args, **kwargs)

    def popen(self):
        line = ' '.join(self.args)
        self.host.history.append(line)
        return FakePopen(self.host, self.response)

    def call(self):
        p = self.popen()
        return p.returncode

    def check_call(self):
        self.popen()

    def check_output(self):
        self.popen()
        return self.response


class FakePopen(object):
    """Fakes subprocess.Popen for FakeProcess."""

    def __init__(self, host, response):
        self.host = host
        self.returncode = 0
        self.stderr = FakePipe()
        self.response = response

    def communicate(self, inputs=None):
        if inputs:
            for line in str(inputs).split('\n'):
                self.host.history.append(self.host.as_input(line))
        return (self.response, '')

    def wait(self, timeout=None):
        return 0


class FakePipe(object):
    """Minimal fake pipe object used by FakePopen."""

    def readline(self):
        return ''
