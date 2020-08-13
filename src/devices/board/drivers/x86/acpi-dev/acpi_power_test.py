#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# ACPI POWER TEST README
#
# This test is intended to validate the basic functionality of the following
# drivers:
#   dev-battery
#   dev-pwrsrc
#
# While not ideal, this test serves to act as validation for these fragments
# at least until there is a more comprehensive test framework capable of
# exercising the full breadth of device enumerations within a given product that
# uses ACPI.
#
# The test uses the command 'fx shell lspwr' which executes the power utility
# on the device under test, obtaining status information from /dev/class/power
# entries.
#
# The test assumes a device which is both battery and AC powered, e.g. Eve or
# Ava, with the typical output of lspwr including both battery and ac device
# details.
#
# Typical output:
# > fx shell lspwr
# [000] type: AC, state: online (0x1)
# [001] type: battery, state: online, charging (0x5)
#              design capacity: 5407 mA
#           last full capacity: 5227 mA
#               design voltage: 7700 mV
#             warning capacity: 811 mA
#                 low capacity: 540 mA
#      low/warning granularity: 1 mA
#     warning/full granularity: 1 mA
#                 present rate: -1508 mA
#           remaining capacity: 5227 mA
#              present voltage: 8441 mV
# ==========================================
# remaining battery percentage: 100 %
#

import subprocess
from os import path
import signal
import sys
import time
import os

CRED = '\x1b[6;30;41m'
CGREEN = '\x1b[6;30;42m'
CEND = '\x1b[0m'


def TestAcDeviceOutput(output):
    print('ac output: ' + output)
    if output.find('[000]') == -1:
        print('...failed to find /dev/class/power/000\n')
        return 0
    if output.find('type: AC') == -1:
        print('...failed to identify AC power source\n')
        return 0

    return 1


def TestBatteryDeviceOutput(output):
    print('battery output: ' + output)
    if output.find('[001]') == -1:
        print('...failed to find /dev/class/power/001\n')
        return 0
    if output.find('type: battery') == -1:
        print('...failed to identify battery device\n')
        return 0

    return 1


def main(argv):
    print('running fx shell lspwr...')
    outLsPwr = subprocess.check_output(['fx', 'shell', 'lspwr']).decode('utf-8')

    print('AC status test:')
    print('---------------\n')
    if TestAcDeviceOutput(outLsPwr.split('\n')[0]):
        print(CGREEN + 'PASSED' + CEND + '\n')
    else:
        print(CRED + 'FAILED' + CEND + '\n')

    print('Battery status test:')
    print('--------------------\n')
    if TestBatteryDeviceOutput(outLsPwr.split('\n')[1]):
        print(CGREEN + 'PASSED' + CEND + '\n')
    else:
        print(CRED + 'FAILED' + CEND + '\n')

    print('test complete')
    return 0


if __name__ == '__main__':
    main(sys.argv)
