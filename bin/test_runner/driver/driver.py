# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os


class Color(object):
  PURPLE = '\033[95m'
  CYAN = '\033[96m'
  DARKCYAN = '\033[36m'
  BLUE = '\033[94m'
  GREEN = '\033[92m'
  YELLOW = '\033[93m'
  RED = '\033[91m'
  BOLD = '\033[1m'
  UNDERLINE = '\033[4m'
  END = '\033[0m'


class Log(object):
  silence = False

  @classmethod
  def print_(cls, message):
    if not cls.silence:
      print message

  @classmethod
  def line(cls, status, message):
    cls.print_('[%s] %s' % (status, message))

  @classmethod
  def command(cls, command):
    cls.line(os.path.basename(command[0]), ' '.join(command))

  @classmethod
  def test_result(cls, result):
    if result['failed']:
      status = 'failed'
    else:
      status = 'passed'
    message = '%s (%d ms)' % (result['name'], result['elapsed'])
    cls.line(status, message)

  @classmethod
  def full_report(cls, result):
    stars = '*' * len(result['name'])
    lines = [
      Color.RED,
      stars,
      result['name'],
      stars,
      result['message'].rstrip(),
      Color.END,
    ]
    for line in lines:
      cls.print_(line)


class NoMatchingTest(Exception): pass


def get_tests(config, name=None):
  by_name = {}
  for test in config['tests']:
    test = dict(test)
    flatten_files_to_copy(test)
    by_name[test['name']] = test

  if name:
    try:
      return [by_name[name]]
    except KeyError:
      raise NoMatchingTest(name)
  else:
    return list(by_name.itervalues())


def flatten_files_to_copy(test):
  copy_map = test.get('copy', {})
  test['copy'] = []
  for remote_dir, files in copy_map.iteritems():
    for filename in files:
      test['copy'].append((filename, remote_dir))


class MultipleDevicesFound(Exception): pass


def get_default_device(netls_output):
  devices = []
  for line in netls_output.splitlines():
    name = line.split()[1]
    devices.append(name)

  if len(devices) > 1:
    raise MultipleDevicesFound(', '.join(devices))

  return devices[0]


class MissingEnvironmentVariable(Exception): pass


class FuchsiaTools(object):
  def __init__(self, env):
    self.env = env

  def _get_env(self, name):
    try:
      return self.env[name]
    except KeyError:
      raise MissingEnvironmentVariable(name)

  @property
  def fuchsia_out_dir(self):
    return self._get_env('FUCHSIA_OUT_DIR')

  @property
  def fuchsia_build_dir(self):
    return self._get_env('FUCHSIA_BUILD_DIR')

  def scp(self, server, filename, remote_dir):
    ssh_config = os.path.join(self.fuchsia_build_dir, 'ssh-keys/ssh_config')
    local_path = os.path.join(self.fuchsia_build_dir, filename)
    command = [
      'scp',
      '-F', ssh_config,
      local_path,
      server + ':' + remote_dir,
    ]
    Log.command(command)
    return command

  def netaddr(self, device):
    path = os.path.join(self.fuchsia_out_dir, 'build-magenta/tools/netaddr')
    command = [
      path,
      '--fuchsia',
      device,
    ]
    Log.command(command)
    return command

  def netls(self):
    path = os.path.join(self.fuchsia_out_dir, 'build-magenta/tools/netls')
    command = [
      path,
      '--timeout=3000',
      '--nowait',
    ]
    Log.command(command)
    return command


class BadOpCode(Exception): pass
class WrongTestId(Exception): pass


class Driver(object):
  """Invokes multiple tests on a remote machine and collects results."""

  def __init__(self):
    self.current_id = None
    self.current_test = None
    self.failed = []
    self.count = 0

  def _on_result(self, result):
    self.count += 1
    Log.test_result(result)
    if result['failed']:
      Log.full_report(result)
      self.failed.append(result)

  def _on_empty_result(self, failed):
    self.count += 1
    status = 'failed' if failed else 'passed'
    Log.line(status, self.current_test['name'])

  def start_test(self, id_, test):
    self.current_id = id_
    self.current_test = test
    Log.line('starting', '%s (%s)' % (test['name'], self.current_id))
    Log.line('executing', test['exec'])
    return 'run %s %s' % (self.current_id, test['exec'])

  def wait_for_teardown(self, iter_messages):
    any_results = False
    for message in iter_messages:
      return_id, op, data = message.split(' ', 2)
      data = data.strip()
      if return_id != self.current_id:
        raise WrongTestId('expected %s got %s' % (self.current_id, return_id))

      if op == 'teardown':
        if not any_results:
          # Currently the Modular tests don't report individual results, they
          # just run an entire command and then send a teardown message. Make
          # sure something gets displayed and counted in that case.
          self._on_empty_result(data == 'fail')
        break
      elif op == 'result':
        any_results = True
        self._on_result(json.loads(data))
      elif op == 'log':
        Log.line('log', data)
      else:
        raise BadOpCode(op)

  def print_summary(self):
    Log.print_('\nRan %d total tests: %d passed, %d failed' % (
        self.count, self.count - len(self.failed), len(self.failed)))

    if self.failed:
      Log.print_('Summary of failures:')
      for result in self.failed:
        Log.full_report(result)
