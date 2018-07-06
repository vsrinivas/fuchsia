#!/usr/bin/env python3
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import collections
import json
import os
import sys


def extract_update_root_keys(manifest_path):
  with open(manifest_path, 'r') as fin:
    manifest = json.loads(fin.read())

  keyids = manifest['signed']['roles']['root']['keyids']
  keys = manifest['signed']['keys']

  rootKeys = []
  for keyid in keyids:
    key = keys[keyid]
    rootKeys.append(collections.OrderedDict((
        ('type', key['keytype']),
        ('value', key['keyval']['public']),
    )))

  return rootKeys


def generate_devhost_config(args):
  manifest_path = os.path.join(args.build_dir, 'root_manifest.json')
  rootKeys = extract_update_root_keys(manifest_path)

  config = collections.OrderedDict((
      ('id', args.name),
      ('repoUrl', args.repo_url),
      ('blobRepoUrl', args.blobs_url),
      ('rateLimit', 0),
      ('ratePeriod', 0),
      ('rootKeys', rootKeys),
  ))

  return config


def parseargs():
  parser = argparse.ArgumentParser(
      description='Generate amber update source config file')

  parser.add_argument(
      '--build-dir',
      required=True,
      help="Fuchsia build output directory")
  parser.add_argument(
      '--name',
      default='devhost',
      help="Name of update source")
  parser.add_argument(
      '--repo-url',
      required=True,
      help="URL of update repository")
  parser.add_argument(
      '--blobs-url',
      required=False,
      help="URL of update blobs repository (<repo-url>/blobs if not specified)")
  parser.add_argument(
      '--output',
      required=True,
      help="Path to write generated config")

  args = parser.parse_args()

  return args


def main(args):
  if not args.blobs_url:
    args.blobs_url = '{0}/blobs'.format(args.repo_url)

  if not os.path.isdir(args.build_dir):
    print("Build directory does not exist", file=sys.stderr)
    return 1

  config = generate_devhost_config(args)

  encoded = json.dumps(config, indent=4)
  with open(args.output, 'w') as fout:
    fout.write(encoded)

  return 0


if __name__ == '__main__':
  sys.exit(main(parseargs()))
