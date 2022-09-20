#!/usr/bin/env python3
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import subprocess
import signal
import shutil
import tempfile
from time import sleep
import unittest
import zipfile

CONNECT_ATTEMPT_LIMIT = 10
DEVICE_NAME = "127.0.0.1:5556"
ADB = '../../prebuilt/starnix/internal/android-image-amd64/adb'
FFX = './host_x64/ffx'
ART_TEST_ZIP = '../../prebuilt/starnix/internal/android-image-amd64/art-run-test-target-data.zip'

ART_TESTS = ['001-HelloWorld']


def run_bridge():
    subprocess.call((FFX, "config", "set", "starnix_enabled", "true"))
    return subprocess.Popen((FFX, "starnix", "adb"), start_new_session=True)


def adb_command(cmd):
    adb = (ADB, '-s', DEVICE_NAME)
    return adb + cmd


def read_file(dir, file):
    full_file = dir + '/' + file
    with open(full_file, 'rb') as f:
        contents = f.read()
        return contents


def connect_to_device():
    connect_string = subprocess.check_output((ADB, "connect", DEVICE_NAME))
    did_connect = connect_string.startswith(b'connected to')
    if not did_connect:
        subprocess.call((ADB, 'disconnect', DEVICE_NAME))
    return did_connect


def extract_test_files(name):
    tmppath = tempfile.mkdtemp()
    jarfile = f'target/{name}/classes.jar'
    refstdout = f'target/{name}/expected-stdout.txt'
    refstderr = f'target/{name}/expected-stderr.txt'
    with zipfile.ZipFile(ART_TEST_ZIP, 'r') as zip:
        zip.extract(jarfile, path=tmppath)
        zip.extract(refstdout, path=tmppath)
        zip.extract(refstderr, path=tmppath)
    return (tmppath, jarfile, refstdout, refstderr)


def boot_classpath():
    jars = (
        'core-libart', 'apache-xml', 'okhttp', 'core-oj', 'service-art',
        'bouncycastle', 'conscrypt')
    boot_cp = '-Xbootclasspath'
    for jar in jars:
        boot_cp += f':/apex/com.android.art/javalib/{jar}.jar'
    boot_cp += ':/apex/com.android.i18n/javalib/core-icu4j.jar'
    return boot_cp


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
            adb_command(("shell", "ls", "-l", "/system/bin/sh")))
        print(result)

    def test_art_java(self):
        for test in ART_TESTS:
            print(f'RUN_TEST: {test}')
            try:
                (tmppath, jarfile, refstdout,
                 refstderr) = extract_test_files(test)
                remotejar = f'/data/arttest/{test}.jar'
                subprocess.call(
                    adb_command(('push', tmppath + '/' + jarfile, remotejar)))

                dalvik_command = adb_command(
                    (
                        'shell', 'dalvikvm64', boot_classpath(), '-classpath',
                        remotejar, 'Main'))
                result = subprocess.run(dalvik_command, capture_output=True)
                refout = read_file(tmppath, refstdout)
                referr = read_file(tmppath, refstderr)
                obsout = result.stdout
                if obsout is None:
                    obsout = ''
                obserr = result.stderr
                if obserr is None:
                    obserr = ''
                self.assertEqual(obsout, refout)
                self.assertEqual(obserr, referr)
                subprocess.call(adb_command(('shell', 'rm', remotejar)))
                print(f'PASSED: {test}')
            finally:
                shutil.rmtree(tmppath)
