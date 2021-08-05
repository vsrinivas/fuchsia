#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Check that all drivers included are in the driver allowlist."""

import json
import argparse
import os


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--allowlist',
        type=argparse.FileType('r', encoding='UTF-8'),
        help='Path to the allowlist of driver labels')
    parser.add_argument(
        '--allowlist_arch_specific',
        type=argparse.FileType('r', encoding='UTF-8'),
        help='Path to the allowlist of architecture specific driver labels')
    parser.add_argument(
        '--driver_list',
        type=argparse.FileType('r', encoding='UTF-8'),
        help='Path to the list of drivers to check against the allowlist')
    parser.add_argument(
        '--output',
        type=argparse.FileType('w', encoding='UTF-8'),
        help='The path for the output file. This tool outputs the new allowlist.'
    )
    parser.add_argument(
        '--contains_all_drivers',
        action='store_true',
        help=
        'If this flag exists then we will also check that every driver in the allowlist exists in driver_list'
    )

    args = parser.parse_args()

    driver_list = args.driver_list.read().splitlines()
    allowlist = args.allowlist.read().splitlines()

    output_name = os.path.realpath(args.output.name)
    allowlist_name = os.path.realpath(args.allowlist.name)

    full_allowlist = set(allowlist)
    if args.allowlist_arch_specific:
        full_allowlist.update(args.allowlist_arch_specific.read().splitlines())

    error = False
    extra_drivers = {
        driver for driver in driver_list
        if driver not in full_allowlist and not driver.startswith("//vendor")
    }

    if len(extra_drivers) > 0:
        print(
            "Error: The following drivers are not in the all_drivers_list.txt:")
        for driver in sorted(extra_drivers):
            print("  " + driver)
        print(
            "If you are adding these drivers, please add them to //build/drivers/all_drivers_list.txt"
        )
        print("You can do this by running the following command:")
        print("   cp " + output_name + " " + allowlist_name)
        print(
            "Also please make sure to include your drivers in //bundles:drivers-build-only"
        )
        error = True

    if args.contains_all_drivers:
        missing_drivers = {
            driver for driver in full_allowlist if driver not in driver_list
        }
        if len(missing_drivers) > 0:
            print("Error: This collection does not have the following drivers:")
            for driver in sorted(missing_drivers):
                print("  " + driver)
            print(
                "Please make sure these drivers are included in //bundles:drivers-build-only"
            )
            error = True

    new_allowlist = set(allowlist)
    new_allowlist.update(extra_drivers)
    args.output.write("\n".join(sorted(new_allowlist)))

    if error:
        raise Exception("Error encountered")


if __name__ == "__main__":
    main()
