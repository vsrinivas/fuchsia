#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from lib.process import Process


class MockProcess(Process):
    """A mock for process creation and execution.

       Instaed of actually running subprocesses, this class just records
       commands. Other mocks can additionally add canned responses.
    """

    def __init__(self, host, args, **kwargs):
        self.host = host
        super(MockProcess, self).__init__(args, **kwargs)

    def popen(self):
        line = ''
        if self.cwd:
            line += 'CWD=%s ' % self.cwd
        line += ' '.join(self.args)
        self.host.history.append(line)

    def call(self):
        self.popen()

    def check_call(self):
        self.popen()

    def check_output(self):
        self.popen()
        return ''
