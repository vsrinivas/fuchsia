# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess
import sys
import tempfile
from threading import Event, Thread
import unittest
from pathlib import Path

TEST_DATA_DIR = 'host_x64/test_data/fidlcat_e2e_tests'  # relative to $PWD
FIDLCAT_TIMEOUT = 60  # timeout when invoking fidlcat

# Convert FUCHSIA_SSH_KEY into an absolute path. Otherwise ffx cannot find
# key and complains "Timeout attempting to reach target".
# See fxbug.dev/101081.
os.environ.update(
    FUCHSIA_ANALYTICS_DISABLED='1',
    FUCHSIA_SSH_KEY=os.path.abspath(os.environ['FUCHSIA_SSH_KEY']),
)


class Ffx:
    _path = 'host_x64/ffx'
    _args = ['--target', os.environ['FUCHSIA_DEVICE_ADDR']]

    def __init__(self, *args: str):
        self.process = subprocess.Popen(
            [self._path] + self._args + list(args),
            text=True,
            stdout=subprocess.PIPE)

    def wait(self):
        self.process.communicate()
        return self.process.returncode


class Fidlcat:
    _path = 'host_x64/fidlcat'
    _args = []
    _ffx_bridge = None

    def __init__(self, *args, merge_stderr=False):
        """
        merge_stderr: whether to merge stderr to stdout.
        """
        assert self._ffx_bridge is not None, 'must call setup first'
        stderr = subprocess.PIPE
        if merge_stderr:
            stderr = subprocess.STDOUT
        self.process = subprocess.Popen(
            [self._path] + self._args + list(args),
            text=True,
            stdout=subprocess.PIPE,
            stderr=stderr)
        self.stdout = ''  # Contains both stdout and stderr, if merge_stderr.
        self.stderr = ''
        self._timeout_cancel = Event()
        Thread(target=self._timeout_thread).start()

    def _timeout_thread(self):
        self._timeout_cancel.wait(FIDLCAT_TIMEOUT)
        if not self._timeout_cancel.is_set():
            self.process.kill()
            self.wait()
            raise TimeoutError('Fidlcat timeouts\n' + self.get_diagnose_msg())

    def wait(self):
        """Wait for the process to terminate, assert the returncode, fill the stdout and stderr."""
        (stdout, stderr) = self.process.communicate()
        self.stdout += stdout
        if stderr:  # None if merge_stderr
            self.stderr += stderr
        self._timeout_cancel.set()
        return self.process.returncode

    def read_until(self, pattern: str):
        """
        Read the stdout until EOF or a line contains pattern. Returns whether the pattern matches.

        Note: A deadlock could happen if we only read from stdout but the stderr buffer is full.
              Consider setting merge_stderr if you want to use this function.
        """
        while True:
            line = self.process.stdout.readline()
            if not line:
                return False
            self.stdout += line
            if pattern in line:
                return True

    def get_diagnose_msg(self):
        return '\n=== stdout ===\n' + self.stdout + '\n\n=== stderr===\n' + self.stderr + '\n'

    @classmethod
    def setup(cls):
        cls._ffx_bridge = Ffx('debug', 'connect', '--agent-only')
        socket_path = cls._ffx_bridge.process.stdout.readline().strip()
        assert os.path.exists(socket_path)
        cls._args = [
            '--unix-connect', socket_path, '--fidl-ir-path', TEST_DATA_DIR,
            '--symbol-path', TEST_DATA_DIR
        ]

    @classmethod
    def teardown(cls):
        cls._ffx_bridge.process.terminate()


# fuchsia-pkg URL for an echo realm. The echo realm contains echo client and echo server components.
# The echo client is an eager child of the realm and will start when the realm is started/run.
#
# Note that the actual echo client is in a standalone component echo_client.cm so we almost always
# need to specify "--remote-component=echo_client.cm" in the test cases below.
ECHO_REALM_URL = 'fuchsia-pkg://fuchsia.com/echo_realm_placeholder#meta/echo_realm.cm'
ECHO_REALM_MONIKER = '/core/ffx-laboratory:fidlcat_test_echo_realm'


class FidlcatE2eTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        Fidlcat.setup()

    @classmethod
    def tearDownClass(cls):
        Fidlcat.teardown()

    # Ensure debug_agent exits correctly after each test case. See fxbug.dev/101078.
    def tearDown(self):
        # FUCHSIA_DEVICE_ADDR and FUCHSIA_SSH_KEY must be defined.
        # FUCHSIA_SSH_PORT is only defined when invoked from `fx test`.
        cmd = [
            'ssh', '-F', 'none', '-o', 'CheckHostIP=no', '-o',
            'StrictHostKeyChecking=no', '-o', 'UserKnownHostsFile=/dev/null',
            '-i', os.environ['FUCHSIA_SSH_KEY']
        ]
        if os.environ.get('FUCHSIA_SSH_PORT'):
            cmd += ['-p', os.environ['FUCHSIA_SSH_PORT']]
        cmd += [
            os.environ['FUCHSIA_DEVICE_ADDR'], 'killall /pkg/bin/debug_agent'
        ]
        res = subprocess.run(
            cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if res.returncode == 0 and 'Killed' in res.stdout:
            print('Killed dangling debug_agent', file=sys.stderr)
        else:
            # The return code will be 255 if no task found so don't check it.
            assert 'no tasks found' in res.stdout, res.stdout

    def test_run_echo(self):
        fidlcat = Fidlcat(
            '--remote-component=echo_client.cm', 'run', ECHO_REALM_URL)
        self.assertEqual(fidlcat.wait(), 0, fidlcat.get_diagnose_msg())

        self.assertIn(
            'sent request test.placeholders/Echo.EchoString = {\n'
            '    value: string = "hello world"\n'
            '  }', fidlcat.stdout)

    def test_stay_alive(self):
        fidlcat = Fidlcat(
            '--remote-name=echo_client', '--stay-alive', merge_stderr=True)
        fidlcat.read_until('Connected!')

        self.assertEqual(
            Ffx('component', 'run', ECHO_REALM_MONIKER, ECHO_REALM_URL).wait(),
            0)
        self.assertEqual(
            Ffx('component', 'destroy', ECHO_REALM_MONIKER).wait(), 0)
        fidlcat.read_until('Waiting for more processes to monitor.')

        # Because, with the --stay-alive version, fidlcat never ends,
        # we need to kill it to end the test.
        fidlcat.process.terminate()
        self.assertEqual(fidlcat.wait(), 0, fidlcat.get_diagnose_msg())

    def test_extra_component(self):
        fidlcat = Fidlcat(
            '--remote-component=echo_client.cm',
            '--extra-component=echo_server.cm', 'run', ECHO_REALM_URL)
        self.assertEqual(fidlcat.wait(), 0, fidlcat.get_diagnose_msg())

        self.assertIn('Monitoring echo_server.cm koid=', fidlcat.stdout)

    def test_trigger(self):
        fidlcat = Fidlcat(
            '--remote-component=echo_client.cm', '--trigger=.*EchoString',
            'run', ECHO_REALM_URL)
        self.assertEqual(fidlcat.wait(), 0, fidlcat.get_diagnose_msg())

        # The first displayed message must be EchoString.
        lines = fidlcat.stdout.split('\n\n')
        self.assertIn(
            'sent request test.placeholders/Echo.EchoString = {', lines[2])

    def test_messages(self):
        fidlcat = Fidlcat(
            '--remote-component=echo_client.cm', '--messages=.*EchoString',
            'run', ECHO_REALM_URL)
        self.assertEqual(fidlcat.wait(), 0, fidlcat.get_diagnose_msg())

        # The first and second displayed messages must be EchoString (everything else has been
        # filtered out).
        lines = fidlcat.stdout.split('\n\n')
        self.assertIn(
            'sent request test.placeholders/Echo.EchoString = {\n'
            '    value: string = "hello world"\n'
            '  }', lines[2])
        self.assertIn(
            'received response test.placeholders/Echo.EchoString = {\n'
            '      response: string = "hello world"\n'
            '    }', lines[3])

    def test_save_replay(self):
        save_path = tempfile.NamedTemporaryFile(suffix='_save.pb')
        fidlcat = Fidlcat(
            '--remote-component=echo_client.cm', '--to', save_path.name, 'run',
            ECHO_REALM_URL)
        self.assertEqual(fidlcat.wait(), 0, fidlcat.get_diagnose_msg())
        self.assertIn(
            'sent request test.placeholders/Echo.EchoString = {\n'
            '    value: string = "hello world"\n'
            '  }', fidlcat.stdout)

        fidlcat = Fidlcat('--from', save_path.name)
        self.assertEqual(fidlcat.wait(), 0, fidlcat.get_diagnose_msg())
        self.assertIn(
            'sent request test.placeholders/Echo.EchoString = {\n'
            '    value: string = "hello world"\n'
            '  }', fidlcat.stdout)

    def test_with_generate_tests_more_than_one_process(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            fidlcat = Fidlcat(
                '--with=generate-tests=' + temp_dir, '--from',
                TEST_DATA_DIR + '/echo.pb')
            fidlcat.wait()
            self.assertIn(
                'Error: Cannot generate tests for more than one process.',
                fidlcat.stdout)

    def test_with_generate_tests(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            fidlcat = Fidlcat(
                '--with=generate-tests=' + temp_dir, '--from',
                TEST_DATA_DIR + '/echo_client.pb')
            self.assertEqual(fidlcat.wait(), 0, fidlcat.get_diagnose_msg())

            self.assertEqual(
                fidlcat.stdout, 'Writing tests on disk\n'
                '  process name: echo_client_cpp\n'
                '  output directory: "{temp_dir}"\n'
                '1412899975 zx_channel_write fuchsia.io/Openable.Open\n'
                '... Writing to "{temp_dir}/fuchsia_io__openable_0.cc"\n'
                '\n'
                '1416045099 zx_channel_write fuchsia.io/Openable.Open\n'
                '... Writing to "{temp_dir}/fuchsia_io__openable_1.cc"\n'
                '\n'
                '1428628083 zx_channel_write fidl.examples.echo/Echo.EchoString\n'
                '1428628083 zx_channel_read fidl.examples.echo/Echo.EchoString\n'
                '... Writing to "{temp_dir}/fidl_examples_echo__echo_0.cc"\n'
                '\n'
                '1430725227 zx_channel_write fuchsia.io/Openable.Open\n'
                '... Writing to "{temp_dir}/fuchsia_io__openable_2.cc"\n'
                '\n'
                '1435967747 zx_channel_write fuchsia.io/Node1.OnOpen\n'
                '... Writing to "{temp_dir}/fuchsia_io__node1_0.cc"\n'
                '\n'
                '1457988959 zx_channel_write fuchsia.sys/Launcher.CreateComponent\n'
                '... Writing to "{temp_dir}/fuchsia_sys__launcher_0.cc"\n'
                '\n'
                '1466376519 zx_channel_read fuchsia.sys/ComponentController.OnDirectoryReady\n'
                '... Writing to "{temp_dir}/fuchsia_sys__component_controller_0.cc"\n'
                '\n'
                '1492595047 zx_channel_read fuchsia.io/Node1.Clone\n'
                '... Writing to "{temp_dir}/fuchsia_io__node1_1.cc"\n'
                '\n'.format(temp_dir=temp_dir))

            # Checks that files exist on disk
            temp = Path(temp_dir)
            self.assertTrue((temp / 'fuchsia_io__openable_0.cc').exists())
            self.assertTrue((temp / 'fuchsia_io__openable_1.cc').exists())
            self.assertTrue((temp / 'fidl_examples_echo__echo_0.cc').exists())
            self.assertTrue((temp / 'fuchsia_io__openable_2.cc').exists())
            self.assertTrue((temp / 'fuchsia_io__node1_0.cc').exists())
            self.assertTrue((temp / 'fuchsia_sys__launcher_0.cc').exists())
            self.assertTrue(
                (temp / 'fuchsia_sys__component_controller_0.cc').exists())
            self.assertTrue((temp / 'fuchsia_io__node1_1.cc').exists())

            # Checks that the generated code is identical to the golden file
            golden = TEST_DATA_DIR + '/fidl_examples_echo__echo.test.cc.golden'
            self.assertEqual(
                (temp / 'fidl_examples_echo__echo_0.cc').read_bytes(),
                Path(golden).read_bytes())

    def test_with_generate_tests_sync(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            fidlcat = Fidlcat(
                '--with=generate-tests=' + temp_dir, '--from',
                TEST_DATA_DIR + '/echo_client_sync.pb')
            self.assertEqual(fidlcat.wait(), 0, fidlcat.get_diagnose_msg())

            self.assertEqual(
                fidlcat.stdout, 'Writing tests on disk\n'
                '  process name: echo_client_cpp_synchronous\n'
                '  output directory: "{temp_dir}"\n'
                '1662590155 zx_channel_write fuchsia.io/Openable.Open\n'
                '... Writing to "{temp_dir}/fuchsia_io__openable_0.cc"\n'
                '\n'
                '1722359527 zx_channel_write fuchsia.io/Openable.Open\n'
                '... Writing to "{temp_dir}/fuchsia_io__openable_1.cc"\n'
                '\n'
                '1950948475 zx_channel_write fuchsia.sys/Launcher.CreateComponent\n'
                '... Writing to "{temp_dir}/fuchsia_sys__launcher_0.cc"\n'
                '\n'
                '2009669511 zx_channel_call fidl.examples.echo/Echo.EchoString\n'
                '... Writing to "{temp_dir}/fidl_examples_echo__echo_0.cc"\n'
                '\n'
                '2085165403 zx_channel_write fuchsia.io/Openable.Open\n'
                '... Writing to "{temp_dir}/fuchsia_io__openable_2.cc"\n'
                '\n'.format(temp_dir=temp_dir))

            # Checks that the generated code is identical to the golden file
            golden = TEST_DATA_DIR + '/fidl_examples_echo__echo_sync.test.cc.golden'
            self.assertEqual(
                Path(temp_dir + '/fidl_examples_echo__echo_0.cc').read_bytes(),
                Path(golden).read_bytes())

    def test_with_generate_tests_server_crashing(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            fidlcat = Fidlcat(
                '--with=generate-tests=' + temp_dir, '--from',
                TEST_DATA_DIR + '/echo_sync_crash.pb')
            self.assertEqual(fidlcat.wait(), 0, fidlcat.get_diagnose_msg())

            self.assertEqual(
                fidlcat.stdout, 'Writing tests on disk\n'
                '  process name: echo_client_cpp_synchronous\n'
                '  output directory: "{temp_dir}"\n'
                '1150113659 zx_channel_write fuchsia.sys/Launcher.CreateComponent\n'
                '... Writing to "{temp_dir}/fuchsia_sys__launcher_0.cc"\n'
                '\n'
                '2223856655 zx_channel_write fuchsia.io/Openable.Open\n'
                '... Writing to "{temp_dir}/fuchsia_io__openable_0.cc"\n'
                '\n'
                '2224905275 zx_channel_write fuchsia.io/Openable.Open\n'
                '... Writing to "{temp_dir}/fuchsia_io__openable_1.cc"\n'
                '\n'
                '2243779711 zx_channel_write fuchsia.io/Openable.Open\n'
                '... Writing to "{temp_dir}/fuchsia_io__openable_2.cc"\n'
                '\n'
                '2674743383 zx_channel_call (crashed) fidl.examples.echo/Echo.EchoString\n'
                '... Writing to "{temp_dir}/fidl_examples_echo__echo_0.cc"\n'
                '\n'.format(temp_dir=temp_dir))

    def test_with_summary(self):
        fidlcat = Fidlcat(
            '--with=summary', '--from', TEST_DATA_DIR + '/echo.pb')
        self.assertEqual(fidlcat.wait(), 0, fidlcat.get_diagnose_msg())

        self.assertEqual(
            fidlcat.stdout,
            '--------------------------------------------------------------------------------'
            'echo_client_cpp.cmx 26251: 26 handles\n'
            '\n'
            '  Process:4cd5cb37(proc-self)\n'
            '\n'
            '  startup Vmar:4cd5cb3b(vmar-root)\n'
            '\n'
            '  startup Thread:4cd5cb3f(thread-self)\n'
            '\n'
            '  startup Channel:4c25cb57(dir:/pkg)\n'
            '\n'
            '  startup Channel:4cb5cb07(dir:/svc)\n'
            '      6857656973.081445 write request  fuchsia.io/Openable.Open\n'
            '\n'
            '  startup Job:4cc5cb17(job-default)\n'
            '\n'
            '  startup Channel:4c85cb0f(directory-request:/)\n'
            '      6857656973.081445 read  request  fuchsia.io/Node1.Clone\n'
            '      6857656977.376411 read  request  fuchsia.io/Openable.Open\n'
            '    closed by zx_handle_close\n'
            '\n'
            '  startup Socket:4cd5cb23(fd:0)\n'
            '    closed by zx_handle_close\n'
            '\n'
            '  startup Socket:4ce5caab(fd:1)\n'
            '    closed by zx_handle_close\n'
            '\n'
            '  startup Socket:4ce5cab3(fd:2)\n'
            '    closed by zx_handle_close\n'
            '\n'
            '  startup Vmo:4cb5cbd7(vdso-vmo)\n'
            '\n'
            '  startup Vmo:4cc5cbdf(stack-vmo)\n'
            '\n'
            '  Channel:4cb5cb93(channel:0)\n'
            '    linked to Channel:4db5cb4f(dir:/svc)\n'
            '    created by zx_channel_create\n'
            '    closed by Channel:4cb5cb07(dir:/svc) sending fuchsia.io/Openable.Open\n'
            '\n'
            '  Channel:4db5cb4f(dir:/svc)\n'
            '    linked to Channel:4cb5cb93(channel:0)\n'
            '    created by zx_channel_create\n'
            '      6857656973.081445 write request  fuchsia.io/Openable.Open\n'
            '    closed by zx_handle_close\n'
            '\n'
            '  Channel:4cc5cba7(server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx)\n'
            '    linked to Channel:4cb5cba3(channel:3)\n'
            '    which is  Channel:83cadf63(directory-request:/svc) in process echo_server_cpp.cmx:26568\n'
            '    created by zx_channel_create\n'
            '      6857656973.081445 write request  fuchsia.io/Openable.Open\n'
            '    closed by zx_handle_close\n'
            '\n'
            '  Channel:4cb5cba3(channel:3)\n'
            '    linked to Channel:4cc5cba7(server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx)\n'
            '    created by zx_channel_create\n'
            '    closed by Channel:4ca5cbab(dir:/svc/fuchsia.sys.Launcher) sending fuchsia.sys/Launcher.CreateComponent\n'
            '\n'
            '  Channel:4ca5cbab(dir:/svc/fuchsia.sys.Launcher)\n'
            '    linked to Channel:4ca5cbaf(channel:5)\n'
            '    created by zx_channel_create\n'
            '      6857656973.081445 write request  fuchsia.sys/Launcher.CreateComponent\n'
            '    closed by zx_handle_close\n'
            '\n'
            '  Channel:4ca5cbaf(channel:5)\n'
            '    linked to Channel:4ca5cbab(dir:/svc/fuchsia.sys.Launcher)\n'
            '    created by zx_channel_create\n'
            '    closed by Channel:4db5cb4f(dir:/svc) sending fuchsia.io/Openable.Open\n'
            '\n'
            '  Channel:4c65cbb3(server-control:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx)\n'
            '    linked to Channel:4c65cbb7(channel:7)\n'
            '    created by zx_channel_create\n'
            '      6857656977.376411 read  event    fuchsia.sys/ComponentController.OnDirectoryReady\n'
            '    closed by zx_handle_close\n'
            '\n'
            '  Channel:4c65cbb7(channel:7)\n'
            '    linked to Channel:4c65cbb3(server-control:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx)\n'
            '    created by zx_channel_create\n'
            '    closed by Channel:4ca5cbab(dir:/svc/fuchsia.sys.Launcher) sending fuchsia.sys/Launcher.CreateComponent\n'
            '\n'
            '  Channel:4c85f443(server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx/fidl.examples.echo.Echo)\n'
            '    linked to Channel:4c45cbbb(channel:9)\n'
            '    which is  Channel:833adf7b(directory-request:/svc/fidl.examples.echo.Echo) in process echo_server_cpp.cmx:26568\n'
            '    created by zx_channel_create\n'
            '      6857656973.081445 write request  fidl.examples.echo/Echo.EchoString\n'
            '      6857656977.376411 read  response fidl.examples.echo/Echo.EchoString\n'
            '    closed by zx_handle_close\n'
            '\n'
            '  Channel:4c45cbbb(channel:9)\n'
            '    linked to Channel:4c85f443(server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx/fidl.examples.echo.Echo)\n'
            '    created by zx_channel_create\n'
            '    closed by Channel:4cc5cba7(server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx) sending fuchsia.io/Openable.Open\n'
            '\n'
            '  Channel:4cc5cb2f(directory-request:/)\n'
            '    created by Channel:4c85cb0f(directory-request:/) receiving fuchsia.io/Node1.Clone\n'
            '      6857656977.376411 write event    fuchsia.io/Node1.OnOpen\n'
            '    closed by zx_handle_close\n'
            '\n'
            '  Channel:4df5f45f(directory-request:/diagnostics)\n'
            '    created by Channel:4c85cb0f(directory-request:/) receiving fuchsia.io/Openable.Open\n'
            '      6857656977.376411 write event    fuchsia.io/Node1.OnOpen\n'
            '    closed by zx_handle_close\n'
            '\n'
            '  Port:4cb5cb8f()\n'
            '    closed by zx_handle_close\n'
            '\n'
            '  Timer:4c85cb8b()\n'
            '    closed by zx_handle_close\n'
            '\n'
            '--------------------------------------------------------------------------------'
            'echo_server_cpp.cmx 26568: 18 handles\n'
            '\n'
            '  Process:839ae00b(proc-self)\n'
            '\n'
            '  startup Vmar:839ae00f(vmar-root)\n'
            '\n'
            '  startup Thread:839ae013(thread-self)\n'
            '\n'
            '  startup Channel:834ae0b7(dir:/pkg)\n'
            '\n'
            '  startup Channel:830ae0cb(dir:/svc)\n'
            '      6857656977.376411 write request  fuchsia.io/Openable.Open\n'
            '\n'
            '  startup Job:839ae0df(job-default)\n'
            '\n'
            '  startup Channel:839ae0d7(directory-request:/)\n'
            '      6857656977.376411 read  request  fuchsia.io/Openable.Open\n'
            '      6857656977.376411 read  request  fuchsia.io/Node1.Clone\n'
            '      6857656977.376411 read  request  fuchsia.io/Openable.Open\n'
            '\n'
            '  startup Socket:839ae0ef(fd:0)\n'
            '\n'
            '  startup Log:839ae0f3(fd:1)\n'
            '\n'
            '  startup Log:839ae0f7(fd:2)\n'
            '\n'
            '  startup Vmo:83bae027(vdso-vmo)\n'
            '\n'
            '  startup Vmo:83aae02f(stack-vmo)\n'
            '\n'
            '  Channel:83dae053(channel:10)\n'
            '    linked to Channel:831ae04b(dir:/svc)\n'
            '    created by zx_channel_create\n'
            '    closed by Channel:830ae0cb(dir:/svc) sending fuchsia.io/Openable.Open\n'
            '\n'
            '  Channel:831ae04b(dir:/svc)\n'
            '    linked to Channel:83dae053(channel:10)\n'
            '    created by zx_channel_create\n'
            '\n'
            '  Channel:83cadf63(directory-request:/svc)\n'
            '    linked to Channel:4cc5cba7(server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx) in process echo_client_cpp.cmx:26251\n'
            '    created by Channel:839ae0d7(directory-request:/) receiving fuchsia.io/Openable.Open\n'
            '      6857656977.376411 read  request  fuchsia.io/Openable.Open\n'
            '    closed by zx_handle_close\n'
            '\n'
            '  Channel:833adf7b(directory-request:/svc/fidl.examples.echo.Echo)\n'
            '    linked to Channel:4c85f443(server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx/fidl.examples.echo.Echo) in process echo_client_cpp.cmx:26251\n'
            '    created by Channel:83cadf63(directory-request:/svc) receiving fuchsia.io/Openable.Open\n'
            '      6857656977.376411 read  request  fidl.examples.echo/Echo.EchoString\n'
            '      6857656977.376411 write response fidl.examples.echo/Echo.EchoString\n'
            '\n'
            '  Channel:83aae003(directory-request:/)\n'
            '    created by Channel:839ae0d7(directory-request:/) receiving fuchsia.io/Node1.Clone\n'
            '      6857656977.376411 write event    fuchsia.io/Node1.OnOpen\n'
            '\n'
            '  Channel:83aae067(directory-request:/diagnostics)\n'
            '    created by Channel:839ae0d7(directory-request:/) receiving fuchsia.io/Openable.Open\n'
        )

    def test_with_top(self):
        fidlcat = Fidlcat('--with=top', '--from', TEST_DATA_DIR + '/echo.pb')
        self.assertEqual(fidlcat.wait(), 0, fidlcat.get_diagnose_msg())

        self.assertEqual(
            fidlcat.stdout,
            '--------------------------------------------------------------------------------'
            'echo_client_cpp.cmx 26251: 11 events\n'
            '  fuchsia.io/Openable: 4 events\n'
            '    Open: 4 events\n'
            '      6857656973.081445 write request  fuchsia.io/Openable.Open(Channel:4cb5cb07(dir:/svc))\n'
            '      6857656973.081445 write request  fuchsia.io/Openable.Open(Channel:4db5cb4f(dir:/svc))\n'
            '      6857656973.081445 write request  fuchsia.io/Openable.Open(Channel:4cc5cba7(server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx))\n'
            '      6857656977.376411 read  request  fuchsia.io/Openable.Open(Channel:4c85cb0f(directory-request:/))\n'
            '\n'
            '  fuchsia.io/Node1: 3 events\n'
            '    OnOpen: 2 events\n'
            '      6857656977.376411 write event    fuchsia.io/Node1.OnOpen(Channel:4cc5cb2f(directory-request:/))\n'
            '      6857656977.376411 write event    fuchsia.io/Node1.OnOpen(Channel:4df5f45f(directory-request:/diagnostics))\n'
            '    Clone: 1 event\n'
            '      6857656973.081445 read  request  fuchsia.io/Node1.Clone(Channel:4c85cb0f(directory-request:/))\n'
            '\n'
            '  fidl.examples.echo/Echo: 2 events\n'
            '    EchoString: 2 events\n'
            '      6857656973.081445 write request  fidl.examples.echo/Echo.EchoString(Channel:4c85f443(server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx/fidl.examples.echo.Echo))\n'
            '      6857656977.376411 read  response fidl.examples.echo/Echo.EchoString(Channel:4c85f443(server:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx/fidl.examples.echo.Echo))\n'
            '\n'
            '  fuchsia.sys/ComponentController: 1 event\n'
            '    OnDirectoryReady: 1 event\n'
            '      6857656977.376411 read  event    fuchsia.sys/ComponentController.OnDirectoryReady(Channel:4c65cbb3(server-control:fuchsia-pkg://fuchsia.com/echo_server_cpp#meta/echo_server_cpp.cmx))\n'
            '\n'
            '  fuchsia.sys/Launcher: 1 event\n'
            '    CreateComponent: 1 event\n'
            '      6857656973.081445 write request  fuchsia.sys/Launcher.CreateComponent(Channel:4ca5cbab(dir:/svc/fuchsia.sys.Launcher))\n'
            '\n'
            '--------------------------------------------------------------------------------'
            'echo_server_cpp.cmx 26568: 8 events\n'
            '  fuchsia.io/Openable: 4 events\n'
            '    Open: 4 events\n'
            '      6857656977.376411 write request  fuchsia.io/Openable.Open(Channel:830ae0cb(dir:/svc))\n'
            '      6857656977.376411 read  request  fuchsia.io/Openable.Open(Channel:839ae0d7(directory-request:/))\n'
            '      6857656977.376411 read  request  fuchsia.io/Openable.Open(Channel:83cadf63(directory-request:/svc))\n'
            '      6857656977.376411 read  request  fuchsia.io/Openable.Open(Channel:839ae0d7(directory-request:/))\n'
            '\n'
            '  fidl.examples.echo/Echo: 2 events\n'
            '    EchoString: 2 events\n'
            '      6857656977.376411 read  request  fidl.examples.echo/Echo.EchoString(Channel:833adf7b(directory-request:/svc/fidl.examples.echo.Echo))\n'
            '      6857656977.376411 write response fidl.examples.echo/Echo.EchoString(Channel:833adf7b(directory-request:/svc/fidl.examples.echo.Echo))\n'
            '\n'
            '  fuchsia.io/Node1: 2 events\n'
            '    Clone: 1 event\n'
            '      6857656977.376411 read  request  fuchsia.io/Node1.Clone(Channel:839ae0d7(directory-request:/))\n'
            '    OnOpen: 1 event\n'
            '      6857656977.376411 write event    fuchsia.io/Node1.OnOpen(Channel:83aae003(directory-request:/))\n'
        )

    def test_with_top_and_unknown_message(self):
        fidlcat = Fidlcat(
            '--with=top', '--from', TEST_DATA_DIR + '/snapshot.pb')
        self.assertEqual(fidlcat.wait(), 0, fidlcat.get_diagnose_msg())

        self.assertIn(
            '  unknown interfaces: : 1 event\n'
            '      6862061079.791403 call   ordinal=36dadb5482dc1d55('
            'Channel:9b71d5c7(dir:/svc/fuchsia.feedback.DataProvider))\n',
            fidlcat.stdout)

    def test_with_messages_and_unknown_message(self):
        fidlcat = Fidlcat(
            '--messages=.*x.*', '--from', TEST_DATA_DIR + '/snapshot.pb')
        self.assertEqual(fidlcat.wait(), 0, fidlcat.get_diagnose_msg())

        # We only check that fidlcat didn't crash.
        self.assertIn(
            'Stop monitoring exceptions.cmx koid 19884\n', fidlcat.stdout)


if __name__ == '__main__':
    unittest.main()
