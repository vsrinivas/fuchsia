# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import itertools
import json
import os
import sys
import unittest

sys.path.append(os.path.dirname(__file__))
from driver import *

Log.silence = True


class ConfigTest(unittest.TestCase):
  COPY_MAP = {
    '/x': ['file1', 'file2'],
    '/y': ['file3'],
  }

  CONFIG_TREE = {
    'tests': [
      {'name': 'test1', 'exec': 'command'},
      {'name': 'test2', 'exec': 'command', 'copy': COPY_MAP},
    ],
  }

  def test_get_all_tests(self):
    tests = get_tests(self.CONFIG_TREE)

    self.assertEqual(len(tests), 2)
    self.assertItemsEqual(tests[0]['copy'], [])

    expected_files = [
      ('file1', '/x'),
      ('file2', '/x'),
      ('file3', '/y'),
    ]
    self.assertItemsEqual(tests[1]['copy'], expected_files)

  def test_get_one_test(self):
    tests = get_tests(self.CONFIG_TREE, 'test2')
    self.assertEqual(len(tests), 1)
    self.assertEqual(tests[0]['name'], 'test2')

  def test_no_matching_test(self):
    with self.assertRaisesRegexp(NoMatchingTest, 'test3'):
      get_tests(self.CONFIG_TREE, 'test3')


class GetDefaultDeviceTest(unittest.TestCase):
  NETLS_SINGLE = 'device step-atom-yard-juicy (fe80::5054:4d:fe63:5e7a/7)\n'
  NETLS_DOUBLE = ('device step-atom-yard-juicy (fe80::5054:4d:fe63:5e7a/7)\n'
        'device brush-santa-rail-creme (fe80::8eae:4c4d:fef4:350d/7)\n')

  def test_success(self):
    device = get_default_device(self.NETLS_SINGLE)
    self.assertEqual(device, 'step-atom-yard-juicy')

  def test_multiple_devices(self):
    expected_msg = 'step-atom-yard-juicy, brush-santa-rail-creme'
    with self.assertRaisesRegexp(MultipleDevicesFound, expected_msg):
      get_default_device(self.NETLS_DOUBLE)


class FuchsiaToolsTest(unittest.TestCase):
  ENV = {'FUCHSIA_OUT_DIR': 'out', 'FUCHSIA_BUILD_DIR': 'build'}

  def test_netaddr(self):
    tools = FuchsiaTools(self.ENV)
    self.assertEqual(
        tools.netaddr('friend')[1:],
        ['netaddr', '--fuchsia', 'friend'])

  def test_netls(self):
    tools = FuchsiaTools(self.ENV)
    self.assertEqual(tools.netls()[1], 'netls')

  # TODO(jimbe) This test should test the contents of the batch file
  def test_sftp(self):
    tools = FuchsiaTools(self.ENV)
    tuple = tools.sftp('server', [('file', '/x')])
    tuple[1].close()
    tuple[0][3] = 'batch';
    self.assertEqual(
        tuple[0][1:],
        ['sftp', '-b', 'batch', 'server'])

  def test_missing_out_dir(self):
    tools = FuchsiaTools({})
    tools.netls()

  def test_missing_build_dir(self):
    tools = FuchsiaTools({})
    with self.assertRaisesRegexp(MissingEnvironmentVariable, 'FUCHSIA_BUILD_DIR'):
      tools.sftp('server', [('file', '/x')])


class DriverTest(unittest.TestCase):
  TEST = {'name': 'test', 'exec': 'whatever'}

  def fake_log(self):
    log = []
    Log.print_ = log.append
    return log

  def result_msg(self, test_id, **result):
    return '%s result %s' % (test_id, json.dumps(result))

  def test_run_many(self):
    log = self.fake_log()
    driver = Driver()

    messages = [
      self.result_msg('111', name='x', elapsed=0, failed=True, message='!'),
      self.result_msg('111', name='y', elapsed=0, failed=False, message=''),
      '111 teardown fail',
    ]
    driver.start_test('111', self.TEST)
    driver.wait_for_teardown(messages)
    self.assertEqual(driver.passed, 1)
    self.assertEqual(driver.failed, 1)
    self.assertEqual(driver.failed_results[0]['message'], '!')

    messages = [
      self.result_msg('222', name='z', elapsed=0, failed=False, message=''),
      '222 teardown pass',
    ]
    driver.start_test('222', self.TEST)
    driver.wait_for_teardown(messages)
    self.assertEqual(driver.passed, 2)
    self.assertEqual(driver.failed, 1)

    driver.print_summary()
    self.assertIn('Ran 3 total tests: 2 passed, 1 failed', log)

  def test_empty_passed_result(self):
    log = self.fake_log()
    driver = Driver()

    driver.start_test('111', self.TEST)
    driver.wait_for_teardown(['111 teardown pass'])
    self.assertEqual(driver.passed, 1)
    self.assertEqual(driver.failed, 0)
    self.assertIn('[passed] test', log)

  def test_empty_failed_result(self):
    log = self.fake_log()
    driver = Driver()

    driver.start_test('111', self.TEST)
    driver.wait_for_teardown(['111 teardown fail'])
    self.assertEqual(driver.passed, 0)
    self.assertEqual(driver.failed, 1)
    self.assertIn('[failed] test', log)

  def test_log(self):
    log = self.fake_log()
    driver = Driver()

    driver.start_test('111', self.TEST)
    driver.wait_for_teardown(['111 log hello', '111 teardown pass'])
    self.assertIn('[log] hello', log)

  def test_wrong_test_id(self):
    driver = Driver()
    driver.start_test('111', self.TEST)
    with self.assertRaisesRegexp(WrongTestId, 'expected 111 got 222'):
      driver.wait_for_teardown(['222 teardown pass'])

  def test_bad_op_code(self):
    driver = Driver()
    driver.start_test('111', self.TEST)
    with self.assertRaisesRegexp(BadOpCode, 'party'):
      driver.wait_for_teardown(['111 party !'])

  def test_missing_teardown(self):
    driver = Driver()
    driver.start_test('111', self.TEST)
    with self.assertRaises(MissingTeardown):
      driver.wait_for_teardown([])


if __name__ == '__main__':
    unittest.main()
