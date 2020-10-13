#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys
import unittest

import test_env
import lib.args
from factory_fake import FakeFactory
from process_fake import FakeProcess
from StringIO import StringIO


class TestCaseWithIO(unittest.TestCase):

    # Unit test "constructor" and "destructor"

    def setUp(self):
        sys.stdin = StringIO()
        self._stdout = StringIO()
        self._stderr = StringIO()

    def tearDown(self):
        sys.stdin = sys.__stdin__

    # Unit test utilities

    def set_input(self, *lines):
        sys.stdin.truncate(0)
        for line in lines:
            sys.stdin.write(line)
            sys.stdin.write('\n')
        sys.stdin.flush()
        sys.stdin.seek(0)

    # Unit test assertions

    def _assert_io_equals(self, io, lines, n=-1):
        io.seek(0)
        self.assertEqual(io.read().split('\n')[:n], list(lines))
        io.truncate(0)

    def assertOut(self, lines, n=-1):
        """Checks that 'n' lines of stdout match 'lines'.

        Calling this method resets stdout. If 'n' is omitted all lines are
        checked. If 'n' is 0 and 'lines' is [], it passes trivially, but still
        resets stdout.
        """
        self._assert_io_equals(self._stdout, lines, n)

    def assertErr(self, lines, n=-1):
        """Checks that 'n' lines of stderr match 'lines'.

        Calling this method resets stdout. If 'n' is omitted all lines are
        checked. If 'n' is 0 and 'lines' is [], it passes trivially, but still
        resets stderr.
        """
        self._assert_io_equals(self._stderr, lines, n)


class TestCaseWithFactory(TestCaseWithIO):
    """TestCase that provides common test context, utilities, and assertions."""

    # Unit test "constructor"

    def setUp(self):
        super(TestCaseWithFactory, self).setUp()
        self._factory = None

    # Unit test context, as aliases to the Factory.

    @property
    def factory(self):
        """The associated FakeFactory object."""
        if not self._factory:
            self._factory = FakeFactory()
            self.host.fd_out = self._stdout
            self.host.fd_err = self._stderr
        return self._factory

    @property
    def host(self):
        """The associated Host object."""
        return self.factory.host

    @property
    def parser(self):
        """The associated ArgParser object."""
        return self.factory.parser

    @property
    def buildenv(self):
        """The associated BuildEnv object."""
        return self.factory.buildenv

    @property
    def device(self):
        """The associated Device object."""
        return self.factory.device

    # Unit test utilities

    def _ssh_cmd(self, args):
        """Returns the command line arguments for an SSH commaned."""
        return ['ssh'] + self.device.ssh_opts() + [self.device.addr] + args

    def _scp_cmd(self, args):
        return ['scp'] + self.device.ssh_opts() + args

    def get_process(self, args, ssh=False):
        cmd = self._ssh_cmd(args) if ssh else args
        return self.host.create_process(cmd)

    def parse_args(self, *args):
        return self.parser.parse_args(args)

    def set_outputs(
            self,
            args,
            outputs,
            start=None,
            end=None,
            returncode=None,
            reset=True,
            ssh=False):
        """Sets what will be returned from the stdout and return code of a fake
        process.

        Providing a start and/or end will schedule the output to be added and/or
        removed, respectively, at a later time; see FakeProcess.schedule.
        Setting reset to True will replace any existing output for the command.
        Setting ssh to true will automatically add the necessary SSH arguments.
        """
        process = self.get_process(args, ssh=ssh)
        if reset:
            process.clear()
        process.schedule(
            '\n'.join(outputs), start=start, end=end, returncode=returncode)

    def set_running(self, url, refresh=True, duration=None):
        """Marks a packaged executable as running on device.

        If refresh is True, this will cause the device to refresh its URLs.
        If a duration is provided, the component will stop running
        after the given duration.
        """
        cmd = ['cs info']
        output = '- URL: {}'.format(url)
        end = None if not duration else self.host.elapsed + duration
        self.set_outputs(cmd, [output], end=end, reset=False, ssh=True)
        if refresh:
            self.device.has_cs_info(url, refresh)

    def touch_on_device(
            self, pathname, start=None, end=None, reset=False, size=1000):
        """Prepares the 'ls' response for a file and its parent directory."""
        parts = pathname.split('/')
        dirname = '/'.join(parts[:-1])
        output = '-rw-r--r-- 1 0 0 {} Dec 25 12:34 {}'.format(size, parts[-1])
        self.set_outputs(
            ['ls', '-l', dirname], [output],
            start=start,
            end=end,
            reset=reset,
            ssh=True)
        self.set_outputs(
            ['ls', '-l', pathname], [output],
            start=start,
            end=end,
            reset=reset,
            ssh=True)

    def symbolize_cmd(self):
        cmd = [
            self.buildenv.symbolizer_exec, '-llvm-symbolizer',
            self.buildenv.llvm_symbolizer
        ]
        for build_id_dir in self.buildenv.build_id_dirs:
            cmd += ['-build-id-dir', build_id_dir]
        return cmd

    def infra_testrunner_cmd(self, out_dir, test_file):
        cmd = [os.path.join(self.buildenv.build_dir, 'host_x64', 'testrunner')] \
            + ['-out-dir', out_dir] \
            + ['-use-runtests', '-per-test-timeout', '600s'] \
            + [test_file]
        return cmd

    # Unit test assertions

    def assertLogged(self, *logs):
        """Asserts logs were generated by calls to host.echo or host.error."""
        self.assertOut(logs)

    def assertError(self, expr, *logs):
        assert logs, 'Missing error message.'
        logs = ['ERROR: {}'.format(logs[0])
               ] + ['       {}'.format(log) for log in logs[1:]]
        with self.assertRaises(SystemExit):
            expr()
        self.assertErr(logs)

    def assertRan(self, *args):
        """Asserts a previous call was made to host.create_process."""
        self.assertIn(' '.join(args), self.host.processes.keys())

    def assertScpTo(self, *args):
        """Asserts a previous call was made to device.scp with args."""
        args = list(args)[:-1] + [self.device.scp_rpath(args[-1])]
        cmd = self._scp_cmd(args)
        self.assertRan(*cmd)

    def assertScpFrom(self, *args):
        """Asserts a previous call was made to device.scp with args."""
        args = [self.device.scp_rpath(arg) for arg in args[:-1]] + [args[-1]]
        cmd = self._scp_cmd(args)
        self.assertRan(*cmd)

    def assertSsh(self, *args):
        """Asserts a previous call was made to device.ssh with cmd."""
        cmd = self._ssh_cmd(list(args))
        self.assertRan(*cmd)


class TestCaseWithFuzzer(TestCaseWithFactory):

    # Unit test "constructor"

    def setUp(self):
        super(TestCaseWithFuzzer, self).setUp()
        self.create_fuzzer('check', 'fake-package1/fake-target1')

    # Unit test context.

    @property
    def fuzzer(self):
        """The most recently created Fuzzer object."""
        assert self._fuzzer, 'No fuzzer created.'
        return self._fuzzer

    @property
    def corpus(self):
        return self.fuzzer.corpus

    @property
    def dictionary(self):
        return self.fuzzer.dictionary

    @property
    def ns(self):
        return self.fuzzer.ns

    # Unit test utilities

    def create_fuzzer(self, *args, **kwargs):
        resolve = kwargs.pop('resolve', True)
        include_tests = kwargs.pop('include_tests', False)
        assert not kwargs, 'Unexpected keyword argument(s): {}'.format(kwargs)
        args = self.parse_args(*args)
        self._fuzzer = self.factory.create_fuzzer(
            args, include_tests=include_tests)
        if resolve:
            self.resolve_fuzzer()
        else:
            base_cmx = self.ns.base_abspath(
                'meta/{}.cmx'.format(self.fuzzer.executable))
            cmd = ['test', '-f', base_cmx]
            process = self.get_process(cmd, ssh=True)
            process.succeeds = False
        self.create_log()
        return self.fuzzer

    def resolve_fuzzer(self):
        cmd = ['pkgctl', 'pkg-status', self.fuzzer.package_url]
        merkle = 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'
        package_path = '/pkgfs/versions/{}'.format(merkle)
        self.set_outputs(
            cmd, [
                'Package in registered TUF repo: yes (merkle={})'.format(
                    merkle),
                'Package on disk: yes (path={})'.format(package_path)
            ],
            ssh=True)

    def create_log(self, start=None, end=None):
        self.touch_on_device(
            self.ns.data_abspath('fuzz-[0-9].log'),
            start=start,
            end=end,
            reset=True)
