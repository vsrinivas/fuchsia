#!/usr/bin/env python2.7
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import errno
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from StringIO import StringIO

import test_env
from lib.cli import CommandLineInterface
from cli_fake import FakeCLI
from test_case import TestCaseWithIO
""" Test the real and fake implementations of the CommandLineInterface.

The structure of these tests is organized into 5 classes:

  NonHermeticTestCase       CommandLineInterfaceTestCase       HermeticTestCase
        ^                          ^      ^                              ^
        |                          |      |                              |
        +-CommandLineInterfaceTest-+      +-FakeCommandLineInterfaceTest-+

NonHermeticTestCase is a TestCase that provides real-system utility functions.
HermeticTestCase is a TestCase that provides faked utility functions.
CommandLineInterfaceTestCase defines a set of tests without a TestCase.
CommandLineInterfaceTest creates tests for CommandLineInterface.
FakeCommandLineInterfaceTest creates tests for FakeCLI.
"""


class NonHermeticTestCase(TestCaseWithIO):
    """TestCase that provides real-system utility functions."""

    # Unit test "constructor" and "destructor"

    def setUp(self):
        super(NonHermeticTestCase, self).setUp()
        self._cli = CommandLineInterface()
        self._temp_dir = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self._temp_dir)
        self._temp_dir = None
        super(NonHermeticTestCase, self).tearDown()

    # Unit test context

    @property
    def cli(self):
        return self._cli

    @property
    def temp_dir(self):
        return self._temp_dir

    # Unit test utilities

    def isfile(self, pathname):
        return os.path.isfile(pathname)

    def isdir(self, pathname):
        return os.path.isdir(pathname)

    def mkdir(self, pathname):
        os.makedirs(pathname)

    def readfile(self, pathname):
        with open(pathname) as opened:
            return opened.read().strip()

    def writefile(self, pathname, contents=None):
        with open(pathname, 'w') as opened:
            if contents:
                opened.write(contents)


class HermeticTestCase(TestCaseWithIO):
    """TestCase that provides faked utility functions."""

    # Unit test "constructor"

    def setUp(self):
        super(HermeticTestCase, self).setUp()
        self._cli = FakeCLI()

        process = self.cli.create_process(['false'])
        process.succeeds = False

        process = self.cli.create_process(['echo foo'])
        process.schedule(start=0, output='foo')

    # Unit test context

    @property
    def cli(self):
        return self._cli

    @property
    def temp_dir(self):
        return 'temp_dir'

    # Unit test utilities

    def isfile(self, pathname):
        return self.cli.isfile(pathname)

    def isdir(self, pathname):
        return self.cli.isdir(pathname)

    def mkdir(self, pathname):
        self.cli.mkdir(pathname)

    def readfile(self, pathname):
        return self.cli.readfile(pathname)

    def writefile(self, pathname, contents=None):
        with self.cli.open(pathname, 'w') as opened:
            if contents:
                opened.write(contents)


class CommandLineInterfaceTestCase(object):
    """Defines a set of tests without a TestCase."""

    def test_echo(self):
        self.cli.echo('Hello world')
        self.assertOut('Hello world')

    def test_error(self):
        with self.assertRaises(SystemExit):
            self.cli.error('Hello world')
        self.assertErr('ERROR: Hello world')

    def test_choose(self):
        prompt = 'Pick your favorite animal'
        choices = ['cat', 'dog', 'bear']

        self.set_input('3')
        choice = self.cli.choose(prompt, choices)
        self.assertEqual(choice, 'bear')

    def test_isdir(self):
        pathname = os.path.join(self.temp_dir, 'test_isdir')
        self.assertFalse(self.cli.isdir(pathname))
        self.mkdir(pathname)
        self.assertTrue(self.cli.isdir(pathname))

    def test_isfile(self):
        pathname = os.path.join(self.temp_dir, 'test_isfile')
        self.assertFalse(self.cli.isfile(pathname))
        self.writefile(pathname)
        self.assertTrue(self.cli.isfile(pathname))

    def test_glob(self):
        foo = os.path.join(self.temp_dir, 'foo')
        bar = os.path.join(self.temp_dir, 'bar')
        self.writefile(foo)
        self.writefile(bar)
        files = self.cli.glob(os.path.join(self.temp_dir, '*'))
        self.assertIn(foo, files)
        self.assertIn(bar, files)

    def test_open(self):
        pathname = os.path.join(self.temp_dir, 'test_open')
        with self.assertRaises(SystemExit):
            opened = self.cli.open(pathname)
        self.assertErr('ERROR: Failed to open {}.'.format(pathname))

        opened = self.cli.open(pathname, missing_ok=True)
        self.assertFalse(opened)

        with self.cli.open(pathname, mode='w') as opened:
            pass

        with self.cli.open(pathname) as opened:
            pass

    def test_readfile(self):
        pathname = os.path.join(self.temp_dir, 'test_readfile')
        with self.assertRaises(SystemExit):
            self.cli.readfile(pathname)
        self.assertErr('ERROR: Failed to open {}.'.format(pathname))

        self.assertFalse(self.cli.readfile(pathname, missing_ok=True))
        self.writefile(pathname, 'data')
        self.assertEqual(self.cli.readfile(pathname), 'data')

    def test_touch(self):
        pathname = os.path.join(self.temp_dir, 'test_touch')
        self.assertFalse(self.isfile(pathname))
        self.cli.touch(pathname)
        self.assertTrue(self.isfile(pathname))

    def test_mkdir(self):
        pathname = os.path.join(self.temp_dir, 'test', 'mkdir')
        self.assertFalse(self.isdir(pathname))
        self.cli.mkdir(pathname)
        self.assertTrue(self.isdir(pathname))

    def test_link(self):
        link = os.path.join(self.temp_dir, 'test_link')

        foo = os.path.join(self.temp_dir, 'foo')
        self.writefile(foo, 'foo')

        bar = os.path.join(self.temp_dir, 'bar')
        self.writefile(bar, 'bar')

        self.cli.link(foo, link)
        self.writefile(link, 'baz')
        self.assertEqual(self.readfile(foo), 'baz')
        self.assertEqual(self.readfile(bar), 'bar')

        self.cli.link(bar, link)
        self.writefile(link, 'qux')
        self.assertEqual(self.readfile(foo), 'baz')
        self.assertEqual(self.readfile(bar), 'qux')

    def test_remove(self):
        pathname = os.path.join(self.temp_dir, 'test_remove')
        self.mkdir(pathname)
        self.assertTrue(self.isdir(pathname))
        self.cli.remove(pathname)
        self.assertFalse(self.isdir(pathname))

        self.writefile(pathname)
        self.assertTrue(self.isfile(pathname))
        self.cli.remove(pathname)
        self.assertFalse(self.isfile(pathname))

    def test_temp_dir(self):
        with self.cli.temp_dir() as temp_dir:
            self.assertTrue(self.isdir(temp_dir.pathname))

    def test_create_process(self):
        self.cli.create_process(['true']).call()
        with self.assertRaises(subprocess.CalledProcessError):
            self.cli.create_process(['false']).check_call()
        output = self.cli.create_process(['echo', 'foo']).check_output()
        self.assertEqual(output, 'foo\n')


class CommandLineInterfaceTest(NonHermeticTestCase,
                               CommandLineInterfaceTestCase):
    """Creates tests for CommandLineInterface."""
    pass


class FakeCommandLineInterfaceTest(HermeticTestCase,
                                   CommandLineInterfaceTestCase):
    """Creates tests for FakeCLI."""
    pass


if __name__ == '__main__':
    unittest.main()
