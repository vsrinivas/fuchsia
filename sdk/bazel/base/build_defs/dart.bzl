# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Contains information about a Dart library's dependency structure.
DartLibraryInfo = provider(
    fields = {
        "package_map": "depset of packages described by struct(name, root)",
    }
)


def _create_info(packages, deps):
    transitive_info = [dep[DartLibraryInfo].package_map for dep in deps]
    result = DartLibraryInfo(
        package_map = depset(packages, transitive = transitive_info),
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


def aggregate_packages(deps):
    '''Aggregates the package mapping provided by the given dependencies.'''
    return _create_info([], deps)


def produce_package_info(package_name, source_root, deps):
    """Aggregates the package mappings provided by the given dependencies, and
       adds an extra mapping from the given name to the given root.
       """
    this_package = struct(name = package_name, root = source_root)
    return _create_info([this_package], deps)


def relative_path(to_path, from_dir=None):
    """Returns the relative path from a directory to a path via the repo root.
       """
    if to_path.startswith("/") or from_dir.startswith("/"):
        fail("Absolute paths are not supported.")
    if not from_dir:
        return to_path
    return "../" * len(from_dir.split("/")) + to_path


def generate_dot_packages_action(context, packages_file, package_info):
    """Creates an action that generates a .packages file based on the packages
       listed in the given info object.
       """
    # Paths need to be relative to the current directory which is under the
    # output directory.
    current_directory = context.bin_dir.path + "/" + context.label.package
    packages = {p.name: relative_path(p.root.path, from_dir=current_directory)
                for p in package_info.package_map.to_list()}

    contents = ""
    for name, path in sorted(packages.items()):
        contents += name + ":" + path + "\n"

    context.actions.write(
        output = packages_file,
        content = contents,
    )


def compile_kernel_action(context, dart_exec, kernel_compiler, sdk_root, main,
                          packages_file, kernel_snapshot_file, manifest_file,
                          main_dilp_file, dilp_list_file):
    """Creates an action that generates the Dart kernel and its dependencies.

    Args:
        context: The rule context.
        dart_exec: The Dart executable `File`.
        kernel_compiler: The kernel compiler snapshot `File`.
        sdk_root: The Dart SDK root `File` (Dart or Flutter's platform libs).
        main: The main `File`.
        packages_file: The .packages `File` output.
        kernel_snapshot_file: The kernel snapshot `File` output.
        manifest_file: The Fuchsia manifest `File` output.
        main_dilp_file: The compiled main dilp `File` output.
        dilp_list_file: The dilplist `File` output.

    Returns:
        Mapping `dict` to be used for packaging.
    """
    # 1. Create the .packages file.
    info = aggregate_packages(context.attr.deps)
    generate_dot_packages_action(context, packages_file, info)
    all_sources = [package.root for package in info.package_map.to_list()]

    # 2. Declare *.dilp files for all dependencies.
    mappings = {}
    for package in info.package_map.to_list():
        dilp_file = context.actions.declare_file(
            context.label.name + "_kernel.dil-" + package.name + ".dilp")
        mappings['data/' + package.name + ".dilp"] = dilp_file

    # 3. Compile the kernel.
    context.actions.run(
        executable = dart_exec,
        arguments = [
            "--no-preview-dart-2",
            kernel_compiler.path,
            "--target",
            "dart_runner",
            "--sdk-root",
            sdk_root.dirname,
            "--packages",
            packages_file.path,
            "--manifest",
            manifest_file.path,
            "--output",
            kernel_snapshot_file.path,
            main.path,
        ],
        inputs = all_sources + [
            kernel_compiler,
            sdk_root,
            packages_file,
            main,
        ],
        outputs = [
            main_dilp_file,
            dilp_list_file,
            kernel_snapshot_file,
            manifest_file,
        ] + mappings.values(),
        mnemonic = "DartKernelCompiler",
    )
    mappings["data/main.dilp"] = main_dilp_file
    mappings["data/app.dilplist"] = dilp_list_file
    return mappings
