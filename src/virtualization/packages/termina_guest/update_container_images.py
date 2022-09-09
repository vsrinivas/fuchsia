#!/usr/bin/env python3
#
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import optparse
import os
import re
import sys
import urllib.request
import subprocess
import json

SCRIPT_DIR = os.path.dirname(__file__)
FUCHSIA_ROOT = os.path.normpath(
    os.path.join(SCRIPT_DIR, "..", "..", "..", ".."))
CIPD_PATH = os.path.join(FUCHSIA_ROOT, ".jiri_root", "bin", "cipd")

CHROMEOS_MILESTONE = "96"
SERVER_URL = "https://storage.googleapis.com/cros-containers/" + CHROMEOS_MILESTONE + "/"


def fuchsia_arch(arch):
    if arch == "amd64":
        return "x64"
    if arch == "arm64":
        return "arm64"
    raise ValueError(f'Invalid arch string {arch}')


def download_images():
    # Download files into //prebuilt directly
    with urllib.request.urlopen(SERVER_URL + "streams/v1/index.json") as index:
        data = json.load(index)
        with urllib.request.urlopen(SERVER_URL +
                                    data['index']['images']['path']) as images:
            images = json.load(images)
            for arch in ["amd64", "arm64"]:
                print(f'Fetching binaries for {fuchsia_arch(arch)}')
                versions = images['products'][f'debian:buster:{arch}:default'][
                    'versions']
                items = versions[next(iter(versions))]['items']
                for f in items:
                    path = items[f]['path']
                    filename = os.path.basename(path)
                    print(f'\t{filename}')
                    filepath = os.path.join(
                        FUCHSIA_ROOT, "prebuilt", "virtualization",
                        "packages", "termina_guest", "container",
                        fuchsia_arch(arch), filename)
                    urllib.request.urlretrieve(SERVER_URL + path, filepath)


def publish_images():
    for arch in ["x64", "arm64"]:
        print(f'Uploading artifacts for {arch}')
        source_dir = os.path.join(
            FUCHSIA_ROOT, "prebuilt", "virtualization", "packages",
            "termina_guest", "container", arch)
        cipd_path = f'fuchsia_internal/linux/termina-container-{arch}'
        output = subprocess.run(
            [
                CIPD_PATH, "create", "-in", source_dir, "-name", cipd_path,
                "-install-mode", "copy"
            ],
            capture_output=True,
            check=True)
        instance = output.stdout.decode("utf-8").lstrip("Instance: ")
        print(f'CIPD Instance for {arch}: {instance}')


def main():
    parser = optparse.OptionParser()
    parser.add_option("--publish", action="store_true")
    options, _ = parser.parse_args()

    if options.publish:
        publish_images()
    else:
        download_images()
        print("Images have been downloaded into //prebuilt. Once you're happy with them, upload to")
        print("CIPD with:")
        print("")
        print("  $ update_container_images.py --publish")
        print("")


if __name__ == '__main__':
    sys.exit(main())
