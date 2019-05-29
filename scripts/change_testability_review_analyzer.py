#!/usr/bin/env python
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script downloads information about changes landed in the Fuchsia tree
# and computes the dates at which those changes receive Code, API, and
# Testability reviews.  It produces four fields into a CSV file:
#
# * Change-ID
# * Time at which the change first received Code-Review+2 and API-Review+1.
#   If a change never received API-Review+1, the time the change first received
#   Code-Review+2 is used. This is an approximation of when the change is first
#   eligible to receive a Testability Review.
# * Time at which the change first received a Testability-Review vote, either
#   +1 or -1.
# * Email of the first reviewer to set Testability-Review
#
# It caches information from Gerrit into //local/change_id_cache/.

import argparse
import datetime
import json
import os
import sys
import requests

FUCHSIA_DIR = os.path.abspath(os.path.join(__file__, os.pardir, os.pardir))


def parse_date_from_gerrit(date_str):
    # Gerrit returns timestamps with 9 digits of precision, but the %f
    # formatter only expects 6 digits of precision, so truncate to parse.
    return datetime.datetime.strptime(date_str[:-3],
                                      "%Y-%m-%d %H:%M:%S.%f")


def cache_dir():
    dirname = os.path.join(FUCHSIA_DIR, 'local', 'change_id_cache')
    if not os.path.exists(dirname):
        os.makedirs(dirname)
    return dirname


def cache_filename(c):
    return os.path.join(cache_dir(), c)


def cache_change_data(c):
    filename = cache_filename(c)
    if os.path.exists(filename):
        return
    print 'Data for %s not found in cache, fetching from gerrit' % c
    url = 'https://fuchsia-review.googlesource.com/changes/%s/detail' % c
    r = requests.get(url)
    if r.status_code != 200:
        print 'Gerrit returned error for %s: %d' % (c, r.status_code)
        return
    response_json = r.text[5:]  # skip preamble
    data = json.loads(response_json)
    with open(cache_filename(c), 'w') as f:
        json.dump(data, f, indent=2)


def read_change_data(c):
    with open(cache_filename(c)) as f:
        return json.load(f)


def process_change(c):
    cache_change_data(c)
    data = read_change_data(c)
    testability_date = None
    code_review_date = None
    api_review_date = None
    for message in data['messages']:
        m = message['message']
        date = parse_date_from_gerrit(message['date'])
        if testability_date is None:
            if 'Testability-Review+1' in m or 'Testability-Review-1' in m:
                testability_reviewer = message['real_author']['email']
                testability_date = parse_date_from_gerrit(message['date'])
        if 'Code-Review+2' in message['message']:
            if code_review_date is None:
                code_review_date = date
        if 'API-Review+1' in message['message']:
            if api_review_date is None:
                api_review_date = date
    if testability_date is None or code_review_date is None:
        return
    last_approval_date = code_review_date
    if api_review_date is not None:
        if api_review_date > last_approval_date:
            last_approval_date = api_review_date
    return (c, testability_date,
            last_approval_date, testability_reviewer)


header = 'ChangeID, First Testability-Review Date, First {Code + API}-Review date, Testability Reviewer'


def main():
    parser = argparse.ArgumentParser(
        description='Compute Testability-Review stats for set of changes.')
    parser.add_argument('--output', '-o', default='output.txt',
                        help='Name of output file')
    parser.add_argument('changes', default='last_week_change_ids.txt',
                        help='File containing Change-Id values to analyze')
    args = parser.parse_args()

    with open(args.changes) as f:
        changes = f.readlines()
    with open(args.output, 'w') as out:
        out.write(header + '\n')
        for c in changes:
            c = c.strip()
            try:
                out.write('%s, %s, %s, %s\n' % process_change(c))
            except KeyboardInterrupt:
                raise
            except:
                sys.stderr.write('Could not process %s, skipping\n' % c)


if __name__ == '__main__':
    sys.exit(main())
