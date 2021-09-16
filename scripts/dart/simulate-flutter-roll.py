#!/usr/bin/env python3

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Utility to simulate a flutter-with-deps roll into Fuchsia.

"""

import argparse
import http.client
import os
import re
import subprocess
import sys
import urllib
import urllib.parse
import urllib.request


def main():
    parser = argparse.ArgumentParser(
        description='Simulate a roll into fuchsia.')
    parser.add_argument(
        '--flutter-repo',
        help='A git repository that has been forked from flutter/flutter.')
    parser.add_argument(
        '--flutter-revision', help='A git hash within the flutter repo')
    args = parser.parse_args()

    # Flutter Repo
    flutter_repo = args.flutter_repo if args.flutter_repo else "flutter"

    # Flutter Revision
    if args.flutter_revision:
        flutter_revision = args.flutter_revision
    else:
        print(f"https://github.com/{flutter_repo}/flutter/commit/HEAD")
        flutter_revision = urllib.request.urlopen(
            f"https://github.com/{flutter_repo}/flutter/commit/HEAD")
        flutter_revision = flutter_revision.read().decode('utf-8').split(
            'sha user-select-contain">')[1].split('</span>')[0]
    print("flutter_revision: " + flutter_revision)

    # Engine Revision
    try:
        engine_revision = urllib.request.urlopen(
            "https://raw.githubusercontent.com/flutter/flutter/" +
            flutter_revision + "/bin/internal/engine.version")
        engine_revision = http.client.parse_headers(
            engine_revision).as_string().strip()
        print("engine_revision: " + engine_revision)
    except urllib.error.HTTPError:
        print('Invalid flutter revision hash.')
        return

    # Dart Revision
    try:
        dart_revision = urllib.request.urlopen(
            "https://raw.githubusercontent.com/flutter/engine/" +
            engine_revision + "/DEPS")
        dart_revision = dart_revision.read().decode('utf-8').split(
            "'dart_revision': '")[1].split("'")[0]
        print("dart_revision: " + dart_revision)
    except urllib.error.HTTPError:
        print('Invalid engine revision hash.')
        return

    update_prebuilts = f"jiri edit -package='fuchsia/dart-sdk/${{platform}}=git_revision:{dart_revision}'\
                                -package='flutter/fuchsia=git_revision:{engine_revision}' \
                                -package='flutter/sky_engine=git_revision:{engine_revision}' \
                                -package='flutter/fuchsia-debug-symbols-x64=git_revision:{engine_revision}' \
                                -package='flutter/fuchsia-debug-symbols-arm64=git_revision:{engine_revision}' \
                                fuchsia/prebuilts"

    update_flutter = f"jiri edit -project='external/github.com/flutter/flutter={flutter_revision}' fuchsia/topaz/flutter"

    os.system(update_prebuilts)
    os.system(update_flutter)

    # Update third_party packages locally.
    os.system("python ../scripts/dart/update_3p_packages.py")

    # # Update lock files.
    os.system("./update-lockfiles.sh")


if __name__ == '__main__':
    sys.exit(main())
