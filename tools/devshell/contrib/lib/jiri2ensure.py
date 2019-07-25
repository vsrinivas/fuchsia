#!/usr/bin/python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os.path
import subprocess
import xml.etree.ElementTree
import sys


FUCHSIA_DIR = os.path.normpath(os.path.join(
    os.path.dirname(__file__), os.pardir, os.pardir, os.pardir, os.pardir))
JIRI_ROOT = os.path.join(FUCHSIA_DIR, '.jiri_root')
CIPD = os.path.join(JIRI_ROOT, 'bin', 'cipd')
LATEST_SNAPSHOT = os.path.join(JIRI_ROOT, 'update_history', 'latest')
CIPD_HOST = 'https://chrome-infra-packages.appspot.com'
# This is the same version currently hard-coded in jiri.
CIPD_VERSION = 'git_revision:65757c8bf7801ec7b3fd3dfae64867fd7f2fc985'


def parse_snapshot(file):
    return xml.etree.ElementTree.parse(file).getroot()


def find_package(snapshot, name):
    return snapshot.findall("./packages/package[@name='%s']" % name)


def generate_ensure_file(prefix, package):
    ensure_file = prefix + '.ensure'
    versions_file = prefix + '.versions'

    body = '''# Generated from jiri snapshot
# Use with cipd tool from:
#   {cipd_host}//client?platform=PLATFORM&version={cipd_version}
# Where PLATFORM can be one of these:
{platforms}
# Copy this file ({ensure}) and the following file to the same directory:
$ResolvedVersions {resolved}
# Then run:
#   cipd ensure -ensure-file .../{ensure} -root SOME/DIR/PATH
# and find the package contents under SOME/DIR/PATH/.
{name} {version}
'''.format(cipd_host=CIPD_HOST,
           cipd_version=CIPD_VERSION,
           ensure=os.path.basename(ensure_file),
           resolved=os.path.basename(versions_file),
           platforms='\n'.join(
               '$VerifiedPlatform %s' % platform
               for platform in package.attrib['platforms'].split(',')),
           name=package.attrib['name'],
           version=package.attrib['version'])

    with open(ensure_file, 'w') as f:
        f.write(body)

    subprocess.check_call(
        [CIPD, 'ensure-file-resolve', '-ensure-file', ensure_file])



def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('package', metavar='PACKAGE',
                        help='CIPD package name as in Jiri manfiest')
    parser.add_argument('prefix', metavar='PREFIX',
                        help='Write PREFIX.ensure and PREFIX.versions')
    args = parser.parse_args()

    pkg = find_package(parse_snapshot(LATEST_SNAPSHOT), args.package)[0]
    generate_ensure_file(args.prefix, pkg)
    return 0


if __name__ == '__main__':
    sys.exit(main())
