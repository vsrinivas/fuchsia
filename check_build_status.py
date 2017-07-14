#!/usr/bin/env python
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import HTMLParser
import os
import sys
import urllib2

BASE_URL = 'https://luci-scheduler.appspot.com/jobs/fuchsia/'


class BuildStatusParser(HTMLParser.HTMLParser):
    def __init__(self):
        HTMLParser.HTMLParser.__init__(self)
        self.success = None

    def handle_starttag(self, tag, attributes):
        if tag != 'tr' or self.success != None:
            return

        for name, value in attributes:
            if name != 'class':
                continue

            if value == 'success':
                self.success = True
            elif value == 'danger':
                self.success = False
            break


def check_build_status(builder):
    try:
        contents = urllib2.urlopen(BASE_URL + builder).read()
        status_parser = BuildStatusParser()
        status_parser.feed(contents)
        status_parser.close()

        if status_parser.success == True:
            return 0
        elif status_parser.success == False:
            return 1
        else:
            print 'ERROR    Could not determine the build status.'
            return 1
    except Exception as err:
        print 'ERROR    Exception thrown while processing: %s' % err.message
        return 1


def get_default_builder_name():
    # In case the 'fset' command of 'env.sh' is being used, try using the
    # currently set build variant by reading the environment variables.
    target = os.getenv('FUCHSIA_GEN_TARGET')
    if target == 'x86-64':
        target = 'x86_64'
    variant = os.getenv('FUCHSIA_VARIANT')
    if target in ['x86_64', 'aarch64'] and variant in ['debug', 'release']:
        # We use "linux" as the host-os even when the host is actually mac,
        # since we don't have mac builders
        return 'fuchsia-%s-linux-%s' % (target, variant)
    else:
        return 'fuchsia-x86_64-linux-debug'


def main():
    default_builder = get_default_builder_name()

    parser = argparse.ArgumentParser(
        '''Check the fuchsia waterfall build status for the specified builder.
This script exits with 0 if the build is currently green, and with 1 otherwise.
''')
    parser.add_argument(
        '--builder',
        '-b',
        help='Name of the builder, e.g. %s' % default_builder,
        default=default_builder)
    args, extras = parser.parse_known_args()
    return check_build_status(args.builder)


if __name__ == '__main__':
    sys.exit(main())
