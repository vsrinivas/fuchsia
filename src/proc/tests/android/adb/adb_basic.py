#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess
import signal
import sys
from time import sleep
import unittest

CONNECT_ATTEMPT_LIMIT = 10
DEVICE_NAME = "127.0.0.1:5556"
ADB = '../../prebuilt/starnix/internal/android-image-amd64/adb'
FFX = './host_x64/ffx'


def run_bridge():
    subprocess.call((FFX, "config", "set", "starnix_enabled", "true"))
    return subprocess.Popen((FFX, "starnix", "adb"), start_new_session=True)


def connect_to_device():
    connect_string = subprocess.check_output((ADB, "connect", DEVICE_NAME))
    did_connect = connect_string.startswith(b'connected to')
    if not did_connect:
        subprocess.call((ADB, 'disconnect', DEVICE_NAME))
    return did_connect


class AdbTest(unittest.TestCase):

    def setUp(self):
        subprocess.call((ADB, "kill-server"))
        self.bridge = run_bridge()
        attempt_count = 0
        is_connected = False
        while not is_connected:
            is_connected = connect_to_device()
            if not is_connected:
                attempt_count += 1
                if attempt_count > CONNECT_ATTEMPT_LIMIT:
                    raise Exception(
                        f'could not connect to device {DEVICE_NAME}')
                sleep(10)

    def tearDown(self):
        os.killpg(os.getpgid(self.bridge.pid), signal.SIGTERM)
        self.bridge.wait()
        subprocess.call((ADB, "kill-server"))

    def test_basic(self):
        result = subprocess.check_output(
            (ADB, "-s", DEVICE_NAME, "shell", "ls", "-l", "/system/bin/sh"))
        print(result)
