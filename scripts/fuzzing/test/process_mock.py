#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from lib.process import Process


class MockProcess(Process):
    """A mock for process creation and execution.

       Instead of actually running subprocesses, this class just records
       commands. Other mocks can additionally add canned responses.
    """

    def __init__(self, host, args, **kwargs):
        self.host = host
        self.response = None
        super(MockProcess, self).__init__(args, **kwargs)

    def popen(self):
        line = ''
        if self.cwd:
            line += 'CWD=%s ' % self.cwd
        line += ' '.join(self.args)
        self.host.history.append(line)
        return MockPopen(self.host, self.response)

    def call(self):
        self.popen()

    def check_call(self):
        self.popen()

    def check_output(self):
        self.popen()
        return self.response


class MockPopen(object):
    """Mocks subprocess.Popen for MockProcess."""

    def __init__(self, host, response):
        self.host = host
        self.returncode = 0
        if response:
            self.response = response
        else:
            self.response = ''

    def communicate(self, inputs=None):
        # if inputs:
        #     lines = inputs.split('\n')
        # else:
        #     lines = []
        for line in inputs:
            self.host.history.append(' < %s' % line)
        return (self.response, '')
