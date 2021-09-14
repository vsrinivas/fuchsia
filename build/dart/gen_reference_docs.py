#!/usr/bin/env python3.8
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Generate Dart reference docs for one or more Fuchsia packages.

This script uses Dartdoc, which documents a single package. If called with more
than one package, an intermediary package is generated in order to document all
the given packages at once; in that case, the --gen-dir argument is required. If
this behavior is not desired, do not pass more than one package argument.
"""

import argparse
import tempfile
import os
import shutil
import subprocess
import sys
import yaml
import generate_dart_toc

_PUBSPEC_CONTENT = """name: Fuchsia
homepage: https://fuchsia.dev/reference/dart
description: API documentation for fuchsia
environment:
  sdk: '>=2.10.0 <3.0.0'
dependencies:
"""


def is_dart_package_dir(package_dir):
    """Returns whether or not a directory resembles a Dart package dir."""
    if not os.path.isdir(package_dir):
        print('%s is not a directory' % (package_dir))
        return False

    if not os.path.isdir(os.path.join(package_dir, 'lib')):
        print('%s is missing a lib subdirectory' % (package_dir))
        return False

    pubspec_file = os.path.join(package_dir, 'pubspec.yaml')
    if not os.path.exists(pubspec_file):
        print('%s is missing a pubspec.yaml' % (package_dir))
        return False

    with open(pubspec_file) as f:
        pubspec = yaml.load(f, Loader=yaml.Loader)
        if not pubspec or pubspec['name'] != os.path.basename(package_dir):
            print('%s has an invalid pubspec.yaml. Ignoring.' % (package_dir))
            return False

    return True


def collect_top_level_files(package_dir):
    """Return a list of dart filenames under the package's lib directory."""
    return sorted(
        os.path.basename(p)
        for p in os.listdir(os.path.join(package_dir, 'lib'))
        if os.path.basename(p).endswith('.dart'))


def compose_pubspec_content(package_dict):
    """Compose suitable contents for a pubspec file.

    The pubspec will have dependencies on the packages in package_dict.

    Args:
        package_dict: Dictionary mapping package name to path.
    Returns:
        String with the pubspec content.
    """
    pubspec_content = _PUBSPEC_CONTENT
    for pkg in package_dict:
        pubspec_content += '  %s:\n    path: %s/\n' % (pkg, package_dict[pkg])
    return pubspec_content


def compose_imports_content(imports_dict):
    """Compose suitable contents for an imports file.

    The contents will include import statements for all items in imports_dict.

    Args:
        imports_dict: Dictionary mapping package name to a list of file names
            belonging to that package.
    Returns:
        String with the imports content.
    """
    lines = []
    for pkg in imports_dict:
        for i in imports_dict[pkg]:
            lines.append("import 'package:%s/%s';\n" % (pkg, i))
    lines.sort()
    return 'library Fuchsia;\n' + ''.join(lines)


def fabricate_package(gen_dir, pubspec_content, imports_content):
    """Write given pubspec and imports content in the given directory.

    Args:
        packages: A list of package directories to use as dependencies.
        gen_dir: The directory for generated files.
    """
    if not os.path.exists(os.path.join(gen_dir, 'lib')):
        os.makedirs(os.path.join(gen_dir, 'lib'))

    # Fabricate pubspec.yaml.
    pubspec_file = os.path.join(gen_dir, 'pubspec.yaml')
    if os.path.exists(pubspec_file):
        os.remove(pubspec_file)
    with open(pubspec_file, 'w') as f:
        f.write(pubspec_content)

    # Fabricate a dart file with all the imports collected.
    lib_file = os.path.join(gen_dir, 'lib', 'lib.dart')
    if os.path.exists(lib_file):
        os.remove(lib_file)
    with open(lib_file, 'w') as f:
        f.write(imports_content)


def walk_rmtree(directory):
    """Manually remove all subdirectories and files of a directory
    via os.walk instead of using
    shutil.rmtree, to avoid registering spurious reads on stale
    subdirectories. See https://fxbug.dev/74084.

    Args:
        directory: path to directory which should have tree removed. 
    """
    for root, dirs, files in os.walk(directory, topdown=False):
        for file in files:
            os.unlink(os.path.join(root, file))
        for dir in dirs:
            full_path = os.path.join(root, dir)
            if os.path.islink(full_path):
                os.unlink(full_path)
            else:
                os.rmdir(full_path)
    os.rmdir(directory)


def generate_docs(
        package_dir,
        out_dir,
        dart_prebuilt_dir,
        run_toc=False,
        delete_artifact_files=False,
        zipped_result=False):
    """Generate dart reference docs.

    Args:
        package_dir: The directory of the package to document.
        out_dir: The output directory for documentation.
        dart_prebuilt_dir: The directory with dart executables (dartdoc, pub).
        run_toc: If true, will generate a toc.yaml file to represent dartdocs.
        delete_artifact_files: If true, will delete unused artifacts in output.
        zipped_result: If true, will zip dartdocs and delete orig. generated docs.
        
    Returns:
        0 if documentation was generated successfully, non-zero otherwise.
    """
    # Run pub over this package to fetch deps.
    # Use temp dir for pub_cache to ensure hermetic build.
    # TODO (https://fxbug.dev/84343) to have all deps locally.
    with tempfile.TemporaryDirectory() as tmpdirname:
        process = subprocess.run(
            [os.path.join(dart_prebuilt_dir, 'pub'), 'get'],
            cwd=package_dir,
            env=dict(os.environ, PUB_CACHE=tmpdirname),
            capture_output=True,
            universal_newlines=True)
        if process.returncode:
            print(process.stderr)
            return 1

        # Clear the docdir first.
        docs_dir = os.path.join(out_dir, "docs")
        pkg_to_docs_path = os.path.join(package_dir, docs_dir)
        if os.path.exists(pkg_to_docs_path):
            walk_rmtree(pkg_to_docs_path)

        # Run dartdoc.
        excluded_packages = ['Dart', 'logging']
        # The flag no-enhanced-reference-lookup was removed in dart
        # SDK 3.0, but this python script only runs in a builder that
        # pins the dart sdk to the stable version.
        # This is a work around fxb/80677 and should be removed once that
        # bug is solved.
        process = subprocess.run(
            [
                os.path.join(dart_prebuilt_dir, 'dartdoc'),
                '--no-validate-links',
                '--auto-include-dependencies',
                '--no-enhanced-reference-lookup',
                '--exclude-packages',
                ','.join(excluded_packages),
                '--output',
                docs_dir,
                '--format',
                'md',
            ],
            cwd=package_dir,
            env=dict(os.environ, PUB_CACHE=tmpdirname),
            capture_output=True,
            universal_newlines=True)
        if process.returncode:
            print(process.stderr)
            return 1

    # Run toc.yaml generation
    if run_toc:
        generate_dart_toc.no_args_main(
            index_file=os.path.join(pkg_to_docs_path, 'index.json'),
            outfile=os.path.join(pkg_to_docs_path, '_toc.yaml'))

    # Delete useless artifact files
    if delete_artifact_files:
        for file in ['__404error.md', 'categories.json', 'index.json']:
            os.remove(os.path.join(pkg_to_docs_path, file))

    # Zip generated docs and delete original docs to make build hermetic.
    if zipped_result:
        pkg_to_out = os.path.join(package_dir, out_dir)
        shutil.make_archive(
            os.path.join(pkg_to_out, 'dartdoc_out'),
            'zip',
            root_dir=pkg_to_out,
            base_dir='docs')
        walk_rmtree(pkg_to_docs_path)
    return 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,  # Prepend help doc with this file's docstring.
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        '-d',
        '--delete-artifact-files',
        action='store_true',
        help=
        'If set will delete __404error.md, categories.json, index.json in the out directory'
    )
    parser.add_argument(
        '-z',
        '--zipped-result',
        action='store_true',
        help=(
            'If set will zip output documentation and delete md files.'
            'This is to make the doc generation process hermetic'))
    parser.add_argument(
        '-r',
        '--run-toc',
        action='store_true',
        help='If set will run generate_toc script on the generated dartdocs.')
    parser.add_argument(
        '-g',
        '--gen-dir',
        type=str,
        required=False,
        help='Location where intermediate files can be generated (should be '
        'different from the output directory). This is required if more '
        'than one package argument is given.')
    parser.add_argument(
        '-o',
        '--out-dir',
        type=str,
        required=True,
        help='Output location where generated docs should go')
    parser.add_argument(
        '--dep-file', type=argparse.FileType('w'), required=True)
    parser.add_argument(
        '-p',
        '--prebuilts-dir',
        type=str,
        required=False,
        default='',
        help="Location of dart prebuilts, usually a Dart SDK's bin directory")

    parser.add_argument(
        'packages', type=str, nargs='+', help='Paths of packages to document')

    args = parser.parse_args()
    if len(args.packages) == 1:
        package_dir = args.packages[0]
    else:
        # Dartdoc runs over a single package only. Fabricate a package that
        # depends on all the other packages and document that one.
        if not args.gen_dir:
            print('ERROR: --gen-dir is required to document multiple packages.')
            parser.print_help()
            return 1

        package_dir = args.gen_dir

        packages_dict = {}
        imports_dict = {}
        input_dart_files = []
        for pkg in args.packages:
            if is_dart_package_dir(pkg):
                package_basename = os.path.basename(pkg)
                packages_dict[package_basename] = os.path.abspath(pkg)
                imports_dict[package_basename] = collect_top_level_files(pkg)
                for root, dirs, files in os.walk(os.path.join(pkg),
                                                 topdown=False):
                    for name in files:
                        if os.path.basename(name).endswith(
                                '.dart') or name == 'pubspec.yaml':
                            input_dart_files.append(os.path.join(root, name))
            else:
                input_dart_files.append(os.path.join(pkg, 'pubspec.yaml'))
        pubspec_content = compose_pubspec_content(packages_dict)
        imports_content = compose_imports_content(imports_dict)
        fabricate_package(package_dir, pubspec_content, imports_content)
    args.dep_file.write(
        '{}: {}\n'.format(
            os.path.join(package_dir, args.out_dir, 'dartdoc_out.zip'),
            ' '.join(input_dart_files)))

    return generate_docs(
        package_dir, args.out_dir, args.prebuilts_dir, args.run_toc,
        args.delete_artifact_files, args.zipped_result)


if __name__ == '__main__':
    sys.exit(main())
