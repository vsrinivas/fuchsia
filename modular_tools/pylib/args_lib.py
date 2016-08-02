# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import logging
import os
import sys


def validate_directory(path):
  if not os.path.exists(path) or not os.path.isdir(path):
    raise argparse.ArgumentError("{0} does not exist".format(path))
  return path


def add_common_args(parser):
  parser.add_argument('--out-dir', type=validate_directory,
                      help='Path to your output dir')

  parser.add_argument('--release', action='store_true',
                      help='Whether to use release')

  parser.add_argument('--target', choices=['linux', 'android', 'fnl'],
                      help='What platform to target')

  parser.add_argument('-v', '--verbose', action="store_true",
                      help="Increase output verbosity")


def parse_common_args(args):
  if args.verbose:
    args.debug_only_pipe = sys.stdout
    logging.basicConfig(level=logging.DEBUG)
  else:
    args.debug_only_pipe = open(os.devnull, 'w')

  args.modular_root_dir = os.path.dirname(
      os.path.dirname(os.path.dirname(os.path.realpath(__file__))))

  args.configuration = 'Release' if args.release else 'Debug'
  if args.target == 'android':
    args.configuration = 'android_%s' % args.configuration

  # If --modular-output-dir isn't specified default to out/output_dir.
  if not args.out_dir:
    args.out_dir = os.path.join(args.modular_root_dir, 'build')
    if not os.path.isdir(args.out_dir):
      print 'Unable to locate output directory, use --out-dir'
      return False
    logging.debug("--out-dir omitted, defaulting to %s", args.out_dir)
  return True
