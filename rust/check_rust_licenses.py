#!/usr/bin/env python
#
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A script to fetch missing LICENSE files when building a vendored repo

# Should be run from top-level of third_party/rust-crates repository.

import argparse
import os
import re
import sys
import urllib2

repo_re = re.compile('\s*repository\s*=\s*"(.*)"\s*$')

def die(reason):
    raise Exception(reason)

def get_repo_path(subdir):
    for line in open(os.path.join(subdir, 'Cargo.toml')):
        m = repo_re.match(line)
        if m:
            return m.group(1)

def find_github_blob_path(path):
    s = path.split('/')
    if s[2] == 'github.com':
        s[2] = 'raw.githubusercontent.com'
    else:
        die('don\'t know raw content path for ' + path)
    if s[-1] == '':
        del s[-1]
    if s[-2] == 'tree':
        del s[-2]
    else:
        s.append('master')
    return '/'.join(s)

def fetch_license(subdir):
    repo_path = get_repo_path(subdir)
    if repo_path is None:
        die('can\'t find repo path for ' + subdir)
    baseurl = find_github_blob_path(repo_path)
    text = []
    # 'LICENCE' is a British English spelling variant used in https://github.com/ebarnard/rust-plist
    for license_filename in ('LICENSE', 'LICENSE-APACHE', 'LICENSE-MIT', 'COPYING', 'LICENCE'):
        url = '/'.join((baseurl, license_filename))
        try:
            resp = urllib2.urlopen(url)
            contents = resp.read()
            if text: text.append('=' * 40 + '\n')
            text.append(url + ':\n\n')
            text.append(contents)
        except urllib2.HTTPError:
            pass
    if not text:
        die('no licenses found under ' + baseurl)
    else:
        license_out = open(os.path.join(subdir, 'LICENSE'), 'w')
        license_out.write(''.join(text))


def check_licenses(directory, verify=False):
    success = True
    os.chdir(directory)
    for subdir in sorted(os.listdir(os.getcwd())):
        # TODO(pylaligand): remove this temporary hack when a new version of
        # the crate is published.
        if (subdir.startswith('magenta-sys') or
                subdir.startswith('fuchsia-zircon-sys')):
            print 'IGNORED  %s' % subdir
            continue
        if subdir.startswith('.') or not os.path.isdir(subdir):
            continue
        license_files = [file for file in os.listdir(subdir)
                         if file.startswith('LICENSE') or
                         file.startswith('license')]
        if license_files:
            print 'OK       %s' % subdir
            continue
        if verify:
            print 'MISSING  %s' % subdir
            success = False
            continue
        try:
            fetch_license(subdir)
            print 'FETCH    %s' % subdir
        except Exception as err:
            print 'ERROR    %s (%s)' % (subdir, err.message)
            success = False
    return success


def main():
    parser = argparse.ArgumentParser(
        'Verifies licenses for third-party Rust crates')
    parser.add_argument('--directory',
                        help='Directory containing the crates',
                        default=os.getcwd())
    parser.add_argument('--verify',
                        help='Simply check whether licenses are up-to-date',
                        action='store_true')
    args = parser.parse_args()
    if not check_licenses(args.directory, verify=args.verify):
        sys.exit(1)


if __name__ == '__main__':
    main()
