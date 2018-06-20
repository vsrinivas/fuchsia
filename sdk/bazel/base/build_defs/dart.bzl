# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Contains information about a Dart library's dependency structure.
DartLibraryInfo = provider(
    fields = {
        "package_map": "depset of packages described by struct(name, root)",
    }
)

def aggregate_packages(package_name, source_root, deps):
    """Aggregates the package mappings provided by the given dependencies, and
       adds an extra mapping from the given name to the given root.
       """
    transitive_info = [dep[DartLibraryInfo].package_map for dep in deps]
    this_package = struct(name = package_name, root = source_root)
    result = DartLibraryInfo(
        package_map = depset([this_package], transitive = transitive_info),
    )

    # Verify duplicates.
    packages = {}
    for package in result.package_map.to_list():
        name = package.name
        path = package.root.path
        if name in packages:
            if path == packages[name]:
                continue
            fail("Found same package %s with two different roots: %s and %s"
                 % (name, packages[name], path))
        packages[name] = path

    return result


def generate_dot_packages_action(context, packages_file, package_info):
    """Creates an action that generates a .packages file based on the packages
       listed in the given info object.
       """
    contents = ""
    packages = {p.name: p.root.path for p in package_info.package_map.to_list()}
    for name, path in sorted(packages.items()):
        contents += name + ":" + path + "\n"
    context.actions.write(
        output = packages_file,
        content = contents,
    )
