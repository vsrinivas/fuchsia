# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from datetime import datetime, timedelta
import json
import os
import subprocess
import sys
import time
import unittest
import urllib.request as request

MAX_HOST_DIFF = timedelta(minutes=1)
MAX_USERSPACE_SYSTEM_DIFF = timedelta(seconds=2)

os.environ.update(FUCHSIA_ANALYTICS_DISABLED='1',)


class Sl4fClient:
    """ Starts, stops and communicates with an SL4F server on a device.

    After instantiating the class, call `start_server` before making SL4F requests.
    """

    # FUCHSIA_DEVICE_ADDR and FUCHSIA_SSH_KEY must be defined.
    def __init__(
            self,
            target: str = os.environ['FUCHSIA_DEVICE_ADDR'],
            port: int = 80,
            ssh_key: str = os.environ['FUCHSIA_SSH_KEY']):
        """Inits Sl4fClient with default parameters."""
        self._ssh_key = ssh_key
        self._target = target
        self._url = f'http://{target}:{port}'

    def is_running(self):
        """Sends an empty http request to the server to verify if it's listening."""
        try:
            with request.urlopen(self._url) as r:
                return r.read() is not None
        except IOError as e:
            return False

    def ssh(self, remote_cmd):
        """Executes an ssh command on the server."""
        cmd = [
            'ssh', '-F', 'none', '-o', 'CheckHostIP=no', '-o',
            'StrictHostKeyChecking=no', '-o', 'UserKnownHostsFile=/dev/null',
            '-i', self._ssh_key
        ]
        # FUCHSIA_SSH_PORT is only defined when invoked from `fx test`.
        if os.environ.get('FUCHSIA_SSH_PORT'):
            cmd += ['-p', os.environ['FUCHSIA_SSH_PORT']]
        cmd += [self._target, remote_cmd]
        return subprocess.run(
            cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def start_server(self, tries=3, is_running_retries=3):
        """Starts the SL4F server on the target using ssh."""
        if self.is_running():
            return True
        for i in range(tries):
            self.ssh("start_sl4f")
            for _ in range(is_running_retries):
                time.sleep(2)
                if self.is_running():
                    return True
        return False

    def request(self, method, params=None):
        """Sends a JSON-RPC request to SL4F."""
        body = {
            'id': '',
            'method': method,
            'params': params,
        }
        req = request.Request(self._url)
        data = json.dumps(body).encode('utf-8')
        req.add_header('Content-Type', 'application/json; charset=utf-8')
        req.add_header('Content-Length', len(data))

        with request.urlopen(req, data) as resp:
            json_response = json.load(resp)
            err = json_response.get('error')
        if err:
            raise Sl4fError(f'Sl4f server responded with error: {err}')
        return json_response['result']

    def is_time_syncronized(self):
        """ Returns whether or not system time on the DUT is synchronized to an external source."""
        return self.request('time_facade.IsSynchronized')

    def system_time(self):
        """ Returns the system time on the DUT in UTC as reported by the standard libraries used
        by sl4f.
        """
        return datetime.utcfromtimestamp(
            self.request('time_facade.SystemTimeMillis') / 1000.0)

    def userspace_time(self):
        """ Returns the system time on the DUT in UTC, as reported by the clock passed to sl4f's
        runtime.
        """
        return datetime.utcfromtimestamp(
            self.request('time_facade.UserspaceTimeMillis') / 1000.0)


class Sl4fError(Exception):
    """Encapsulates any Sl4f related runtime errors."""
    pass


class TimeSyncE2eTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        sl4f = Sl4fClient()
        cls._sl4f = sl4f
        assert (sl4f.start_server())
        while not sl4f.is_time_syncronized():
            time.sleep(2)

    def testSystemTimeSyncronized(self):
        host_now_utc = datetime.utcnow()
        remote_system_time = self._sl4f.system_time()
        # The UTC clock on the DUT measured directly should roughly align with the host time.
        assert (remote_system_time > host_now_utc - MAX_HOST_DIFF)
        assert (remote_system_time < host_now_utc + MAX_HOST_DIFF)

    def testUserspaceTimeSyncronized(self):
        host_now_utc = datetime.utcnow()
        remote_userspace_time = self._sl4f.userspace_time()
        # The UTC clock on the DUT measured through the runtime should roughly align with the
        # host time.
        assert (remote_userspace_time > host_now_utc - MAX_HOST_DIFF)
        assert (remote_userspace_time < host_now_utc + MAX_HOST_DIFF)

    def testUtcClocksAgree(self):
        host_now_utc = datetime.utcnow()
        remote_system_time = self._sl4f.system_time()
        remote_system_time_offset = datetime.utcnow() - remote_system_time
        remote_userspace_time = self._sl4f.userspace_time()
        remote_userspace_time_offset = datetime.utcnow() - remote_userspace_time
        assert (
            abs(remote_system_time_offset - remote_userspace_time_offset) <
            MAX_USERSPACE_SYSTEM_DIFF)
