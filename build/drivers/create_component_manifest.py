#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Create a Component Manifest from a list of items"""

import json
import argparse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--distribution_manifest_file',
        type=argparse.FileType('r', encoding='UTF-8'),
        help=
        'Path to a distribution_manifest file which is a JSON file that contains driver paths.'
    )
    parser.add_argument(
        '--output',
        type=argparse.FileType('w', encoding='UTF-8'),
        help='The path where this script will output the driver manifest.')
    parser.add_argument(
        '--is_v1',
        action='store_const',
        const=True,
        default=False,
        help=
        'Generates a DFv1 component manifest. (If this is not included a DFv2 manifest is generated)'
    )
    parser.add_argument(
        '--colocate',
        action='store_true',
        help='If this exists then the driver should be colocated with its parent'
    )
    parser.add_argument(
        '--device_categories_file',
        help=
        'A path to a JSON file of device categories to enlist and run the tests for certification'
    )
    parser.add_argument(
        '--fallback',
        action='store_true',
        help='Whether or not the driver is a fallback driver')
    parser.add_argument(
        '--root_resource',
        action='store_true',
        help='Whether or not to give the driver access to the root resource')
    parser.add_argument(
        '--profile_provider',
        action='store_true',
        help=
        'Whether or not to give the driver access to fuchsia.scheduler.ProfileProvider',
    )
    parser.add_argument(
        '--sysmem',
        action='store_true',
        help=
        'Whether or not to give the driver access to fuchsia.sysmem.Allocator',
    )
    parser.add_argument(
        '--default_dispatcher_opts',
        nargs="*",
        help=
        'A space separated list of options for creating the default dispatcher',
    )

    args = parser.parse_args()

    distribution_manifest = json.load(args.distribution_manifest_file)

    program = False
    bind = False

    for entry in distribution_manifest:
        destination = entry['destination']
        if destination.startswith("driver/"):
            if destination == "driver/compat.so":
                continue
            if program:
                raise Exception(
                    "fuchsia_driver_component cannot depend on two drivers: " +
                    program + " " + destination)
            program = destination
        if destination.startswith("meta/bind/"):
            if bind:
                raise Exception(
                    "fuchsia_driver_component cannot depend on two bind programs: "
                    + bind + " " + destination)
            bind = destination

    if not program:
        raise Exception("fuchsia_driver_component must contain a driver")
    if not bind:
        raise Exception("fuchsia_driver_component must contain a bind file")

    manifest = {
        'program':
            {
                'runner': 'driver',
                'bind': bind,
                'fallback': 'true' if args.fallback else 'false',
                'default_dispatcher_opts': ['allow_sync_calls']
            },
        'use': []
    }
    if args.is_v1:
        manifest["program"]["compat"] = program
        manifest["include"] = [
            'inspect/client.shard.cml',
            'syslog/client.shard.cml',
            '//sdk/lib/driver/compat/compat.shard.cml',
        ]
    else:
        manifest["program"]["binary"] = program

    if args.device_categories_file:
        with open(args.device_categories_file) as f:
            manifest["program"]["device_categories"] = json.load(f)

    if args.colocate:
        manifest["program"]["colocate"] = "true"

    if args.root_resource:
        manifest['use'].append({'protocol': "fuchsia.boot.RootResource"})
    if args.profile_provider:
        manifest['use'].append(
            {'protocol': "fuchsia.scheduler.ProfileProvider"})
    if args.sysmem:
        manifest['use'].append({'protocol': "fuchsia.sysmem.Allocator"})
    if args.default_dispatcher_opts:
        manifest["program"][
            "default_dispatcher_opts"] = args.default_dispatcher_opts

    json_manifest = json.dumps(manifest)
    args.output.write(json_manifest)


if __name__ == "__main__":
    main()
