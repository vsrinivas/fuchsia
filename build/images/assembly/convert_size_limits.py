# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
'''Generates a budgets configuration file for size checker tool.

This tool converts the old size budget file to the new format as part of RFC-0144 migration plan.

Usage example:
  python3 build/images/assembly/convert_size_limits.py \
    --size_limits out/default/size_checker.json \
    --product_config out/default/obj/build/images/fuchsia/fuchsia_product_config.json \
    --output out/default/size_budgets.json


The budget configuration is a JSON file used by `ffx assembly size_check` that contains the
list of package groups.

A component budget is an object with the following fields:
  name: string, human readable name of the component.
  budget_bytes: decimal, number of bytes alloted on the devices for the packages of this component.
  packages:  [string], list of paths of `package_manifest.json` files for each of the packages
    conforming to https://source.corp.google.com/fuchsia/src/sys/pkg/lib/fuchsia-pkg/src/package_manifest.rs
'''
import argparse
import json
import sys
import collections
import os


def get_all_manifests(image_assembly_config_json):
    """Returns the list of all the package manifests mentioned in the specified product configuration.

  Args:
    image_assembly_config_json: Dictionary, holding manifest per categories.
  Returns:
    list of path to the manifest files as string.
  Raises:
    KeyError: if one of the manifest group is missing.
  """
    try:
        return image_assembly_config_json.get(
            "base", []) + image_assembly_config_json.get(
                "cache", []) + image_assembly_config_json.get("system", [])
    except KeyError as e:
        raise KeyError(
            f"Product config is missing in the product configuration: {e}")


def convert_budget_format(component, manifests):
    """Converts a component budget to the new budget format.

  Args:
    component: dictionary, former size checker configuration entry.
    manifests: [string], list of path to packages manifests.
  Returns:
    dictionary, new configuration with a name, a maximum size and the list of
    packages manifest to fit in the budget.
  """
    # Ensures each directories is suffixed with exactly one '/',
    # so that 'abc/d' does not match 'abc/def/g.json'
    prefixes = tuple(os.path.join("obj", src, "") for src in component["src"])
    # Finds all package manifest files located bellow the directories
    # listed by the component `src` field.
    packages = [m for m in manifests if m.startswith(prefixes)]
    return dict(
        name=component["component"],
        budget_bytes=component["limit"],
        packages=packages)


def count_packages(budgets, all_manifests):
    """Returns packages that are missing, or present in multiple budgets."""
    package_count = collections.Counter(
        package for budget in budgets for package in budget["packages"])
    more_than_once = [
        package for package, count in package_count.most_common() if count > 1
    ]
    zero = [package for package in all_manifests if package_count[package] == 0]
    return more_than_once, zero


def main():
    parser = argparse.ArgumentParser(
        description=
        'Converts the old size_checker.go budget file to the new format as part of RFC-0144'
    )
    parser.add_argument(
        '--size_limits', type=argparse.FileType('r'), required=True)
    parser.add_argument(
        '--image_assembly_config', type=argparse.FileType('r'), required=True)
    parser.add_argument('--output', type=argparse.FileType('w'), required=True)
    parser.add_argument('-v', '--verbose', action='store_true')
    args = parser.parse_args()

    # Read all input configurations.
    size_limits = json.load(args.size_limits)
    image_assembly_config = json.load(args.image_assembly_config)

    # Convert the configuration to the new format.
    all_manifests = get_all_manifests(image_assembly_config)
    component_budgets = [
        convert_budget_format(component, all_manifests)
        for component in size_limits.get("components", [])
    ]

    # Verify bipartite mapping between manifests and components.
    more_than_once, zero = count_packages(component_budgets, all_manifests)

    if zero and args.verbose:
        print("WARNING: Package(s) not matched by any size budget:")
        for package in zero:
            print(f" - {package}")
        print(f"Review components budgets in {args.size_limits.name}")

    if more_than_once:
        print("ERROR: Package(s) matched by more than one size budget:")
        for package in more_than_once:
            print(f" - {package}")
        return 1  # Exit with an error code.

    # Format the resulting configuration file.
    json.dump(component_budgets, args.output, indent=2)
    return 0


if __name__ == '__main__':
    sys.exit(main())
