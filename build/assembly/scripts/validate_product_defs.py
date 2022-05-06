# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Validate that product definitions are no longer using deprecated GN args.

For every product definition file in the tree, ensure that it is not setting
any GN arg that is now deprecated.

Product definition files are defined to be:
  - //products/**/*.gni
  - //vendor/*/products/**/*.gni

The currently disallowed GN args are:
  - base_package_labels
  - cache_package_labels
  - universe_package_labels

This _does_ allow the GN args to be set to empty strings, the regex match looks
ahead for ` = []` and will not match on that.

"""

import argparse
import os
import re
import sys
from depfile import DepFile
from typing import List, Tuple

disallowed_gn_args = [
    "base_package_labels", "cache_package_labels", "universe_package_labels"
]
gn_args_group = '(' + '|'.join(disallowed_gn_args) + ')'
assignment_matcher_string = r'^\s*' + gn_args_group + r'(?!\s*=\s*\[\])'
assignment_matcher = re.compile(assignment_matcher_string)


def find_product_defs(dir: str) -> List[str]:
    results = []
    with os.scandir(dir) as contents:
        for entry in contents:
            if entry.is_dir():
                results.extend(find_product_defs(entry.path))
            elif entry.is_file() and entry.name.endswith(".gni"):
                results.append(entry.path)
    return results


def find_vendor_product_defs(dir: str) -> List[str]:
    results = []
    vendor_dir = os.path.join(dir, "vendor")
    if os.path.isdir(vendor_dir):
        with os.scandir(vendor_dir) as contents:
            for entry in contents:
                if entry.is_dir():
                    vendor_products_dir = os.path.join(
                        dir, "vendor", entry.name, "products")
                    if os.path.isdir(vendor_products_dir):
                        results.extend(find_product_defs(vendor_products_dir))
    return results


def validate_product_def(path: str) -> List[Tuple[int, str]]:
    """Validate that the product def at 'path' doesn't use deprecated GN args.

    This returns a list of (<line number>, <error>) tuples for the file, it does
    not early-return, so that all errors can be collected for a file.
    """
    results = []
    with open(path) as product_def_file:
        for line_num, line in enumerate(product_def_file.readlines()):
            match = assignment_matcher.match(line)
            if match:
                results.append((line_num, line))
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-root", default=".", help="The root Fuchsia source dir")
    parser.add_argument(
        "--output",
        type=argparse.FileType('w'),
        help="A file to write output to.")
    parser.add_argument(
        "--depfile",
        type=argparse.FileType('w'),
        help="A depfile of read files, this requires the use of --output")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    # Setup logging / error-printing methods and utilities
    output = []

    def log(string: str):
        """log (display or create output for) a non-error string"""
        if args.verbose:
            print(string)
        if args.output:
            output.append(string)

    def error(string: str):
        """output an error string"""
        print(string, file=sys.stderr)
        if args.output:
            output.append(string)

    def format_path(path: str) -> str:
        """Format a path for clearer displaying"""
        path = os.path.relpath(path, args.source_root)
        if os.path.isabs(path):
            return path
        else:
            return "//" + path

    log("Disallowed GN args:")
    for arg in disallowed_gn_args:
        log(f"  {arg}")
    log(f"\nUsing regex:\n  {assignment_matcher_string}\n")

    # Gather the fuchsia.git product definitions
    source_root = args.source_root
    products_dir = os.path.join(source_root, "products")
    product_def_paths = find_product_defs(products_dir)

    # Gather the vendor product definitions (if they exist)
    product_def_paths.extend(find_vendor_product_defs(source_root))

    log("Scanning product defs:")
    results = {}
    for path in sorted(product_def_paths):
        result = validate_product_def(path)
        if result:
            results[path] = result
            log(f"  {format_path(path)}: FAIL")
        else:
            log(f"  {format_path(path)}: PASS")

    if results:
        error(
            "\nFound use of deprecated / disallowed GN arg in product definition:"
        )
        for (path, errors) in results.items():
            error(f"  {format_path(path)}")
            for line_num, error_string in errors:
                error(f"    {line_num}: {error_string}")
        return -1

    if args.depfile:
        if args.output:
            depfile = DepFile(args.output.name)
            depfile.update(product_def_paths)
            depfile.write_to(args.depfile)
        else:
            error("Cannot create a depfile without an output file")
            return -2

    if args.output:
        for line in output:
            print(line, file=args.output)

    return 0


if __name__ == "__main__":
    sys.exit(main())
