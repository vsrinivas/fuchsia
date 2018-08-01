#!/usr/bin/env python

# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys

def gen_pkg_key(pm_bin, dest):
    """Create a key to use for signing FARs.

    pm_bin: path to the 'pm' binary
    dest  : path where the key should be written

    If successful None is returned, otherwise a string describing the error
    encountered is returned.
    """
    pm_cmd = [pm_bin, "-k", dest, "genkey"]
    try:
        subprocess.check_call(pm_cmd)
        return None
    except subprocess.CalledProcessError as e:
        return "Error generating FAR signing key: %s" % e
    except OSError as e:
        return "Error launching PM binary: %s" % e

def build_package(pm_bin, pkg_key, far_stg_dir, manifest, pkg_name):
    """Build a metadata FAR describing the package.

    pm_bin     : path to the 'pm' binary
    pkg_key    : path to the key to use to sign the FAR
    far_stg_dir: a working directory to use for staging files before they are
                 written into a single file
    manifest   : path to a manifest file describing the contents of the package
    pkg_name   : the name of the package

    Create a signed metadata FAR representing the package. This function
    returns a tuple which is the path to the metadata FAR and any error. If
    there is an error the first member of the tuple is None and the second
    member contains a string describing the error. If there is no error, the
    second member of tuple is None.
    """
    init_cmd = [pm_bin, "-o", far_stg_dir, "-n", pkg_name, "init"]
    try:
        subprocess.check_call(init_cmd)
    except subprocess.CalledProcessError as e:
        return None, None, "Could not initialize package: %s" % e
    except OSError as e:
        return None, None, "Could not start package initializer: %s" % e

    build_cmd = [pm_bin, "-o", far_stg_dir, "-k", pkg_key, "-m", manifest, "build"]
    try:
        subprocess.check_call(build_cmd)
    except subprocess.CalledProcessError as e:
        return None, None, "Could not create package metadata FAR: %s" % e
    except OSError as e:
        return None, None, "Could not start packging tool %s" % e

    far_path = os.path.join(far_stg_dir, "meta.far")
    pkg_json = os.path.join(far_stg_dir, "meta", "package")
    if os.path.exists(far_path) and os.path.exists(pkg_json):
        return far_path, pkg_json, None
    else:
        return None, None, "Unknown failure, metadata package not produced"

def assemble_manifest(manifests_dir, output_stream):
    """Create a single manifest from the joining of the system and boot
    manifests.

    manifests_dir: Directory which contains system_manifest and/or
                   boot_manifest.
    output_stream: An output stream where the combined manifest will be
                   written.

    No returns, but may raise an exception if writing to output_stream fails.
    """
    inputs = ["final_package_manifest", "package_manifest"]
    for input in inputs:
        manifest = os.path.join(manifests_dir, input)
        if os.path.exists(manifest) and os.stat(manifest).st_size > 0:
            with open(manifest, "r") as src:
                for line in src:
                    output_stream.write(line)

def add_far_to_repo(amber_bin, name, far, key_dir, repo_dir, ver_scheme, version=0):
    """Add a FAR to the update repository under the specified name

    amber_bin : path to the amber binary
    name      : name to publish the package as
    far       : path to the FAR file to publish
    key_dir   : directory containing set of keys for the update repository
    repo_dir  : directory to use as the update repository (should exist, but
                doesn't need to be initialized as an update repo)
    ver_scheme: scheme to base repository versions on, defaults to "mono"
                for "monotonic" but can be set to "time" to base versions
                on the number of seconds since amber epoch.

    On success returns None, otherwise returns a string describing the error
    that occurred.
    """
    cmd = [amber_bin, "-r", repo_dir, "-p", "-f", far, "-n", "%s/%d" % (name, version), "-k", key_dir]

    if ver_scheme == "time":
      cmd += ["-vt"]
    try:
        subprocess.check_call(cmd)
    except subprocess.CalledProcessError as e:
        return "Failure running package publisher: %s" % e
    except OSError as e:
        return "Failure launching package publisher: %s" % e

    return None

def add_rsrcs_to_repo(amber_bin, manifest, key_dir, repo_dir):
    """Add package resources, aka. content blobs to the update repository.

    amber_bin: path to the amber binary
    manifest : a file containing a mapping of file paths on the target to
               paths on the host. All paths will be added to the update
               respository, named after their content ID.
    key_dir  : directory containing set of keys for the update repository
    repo_dir : directory to use as the update repository (should exist, but
               doesn't need to be initialized as an update repo)

    On success returns None, otherwise returns a string describing the error
    that occurred.
    """
    cmd = [amber_bin, "-r", repo_dir, "-m", "-f", manifest, "-k", key_dir]

    try:
        subprocess.check_call(cmd)
    except subprocess.CalledProcessError as e:
        return "Failure adding content blob: %s" % e
    except OSError as e:
        return "Unable to launch blob tool: %s" % e

    return None

def publish(pm_bin, amber_bin, pkg_key, repo_key_dir, pkg_stg_dir, update_repo,
            manifests_dir, pkgs, ver_scheme, verbose):
    """Publish packages as a signed metadata FAR and a collection of content
    blobs named after their content IDs.

    pm_bin       : path to the pm binary
    amber_bin    : path to the amber binary
    pkg_key      : path to the key to use to sign the metdata FARs
    repo_key_dir : directory containing keys to use for the update respository
    pkg_stg_dir  : a directory that can be used for staging temporary files
                   when creating the packages
    update_repo  : a path that is or can be used as the update repository
    manifests_dir: parent directory where manifests can be found for the
                   packages
    pkgs         : pkgs to publish
    ver_scheme   : scheme to base repository versions on, defaults to "mono"
                   for "monotonic" but can be set to "time" to base versions
                   on the number of seconds since amber epoch.
    verbose      : print out status updates
    """
    # as an optimization, publish the content blobs all at once
    master_fest = os.path.join(pkg_stg_dir, "masterfest")
    master_fd = open(master_fest, "w+")
    count = len(pkgs)
    for pkg in pkgs:
        # this process can be time consuming with a large number of packages
        # so printing incremental progress can be reassuring
        if verbose:
            sys.stdout.write("Packages remaining: %d       \r" % count)
            sys.stdout.flush()

        far_base = os.path.join(pkg_stg_dir, pkg)
        far_stg = os.path.join(far_base, "archive")

        if os.path.exists(far_base):
            shutil.rmtree(far_base)
        os.makedirs(far_stg)

        manifest = os.path.join(far_base, "manifest")
        src_manifest_dir = os.path.join(manifests_dir, pkg)
        with open(manifest, "w+") as man_fd:
            assemble_manifest(src_manifest_dir, man_fd)

        # some packages are apparently devoid of content, skip them
        if os.stat(manifest).st_size == 0:
            count -= 1
            continue

        with open(manifest, "r") as man_fd:
            for line in man_fd:
                master_fd.write(line)

        meta_far, pkg_json, err = build_package(pm_bin, pkg_key, far_stg, manifest, pkg)
        if err is not None:
            print "Building package failed: %s" % err
            break

        pkg_version = None
        with open(pkg_json, 'r') as pkg_meta:
            meta = json.load(pkg_meta)
            pkg_version = int(meta["version"])

        if pkg_version is None:
            print "Could not read version from %q" % pkg_json
            break

        result = add_far_to_repo(amber_bin, pkg, meta_far, repo_key_dir, update_repo,
                                 ver_scheme, version=pkg_version)
        if result is not None:
            print "Package not added to update repo: %s" % result
            break
        count -= 1

    master_fd.close()
    result = add_rsrcs_to_repo(amber_bin, master_fest, repo_key_dir, update_repo)
    if result is not None:
        print "Package contents not added to update repo: %s" % result
        return -1

    return count

def main():
    parser = argparse.ArgumentParser(description=("Publish one or more build "
                                                  "packages as package manager "
                                                  "packages."))
    parser.add_argument('--build-dir', action='store', required=True)
    parser.add_argument('--host-tools-dir', action='store', required=False,
                        help="""Directory where host tools such as 'pm' live""")
    parser.add_argument('--update-repo', action='store', required=False)
    parser.add_argument('--update-keys', action='store', required=False)
    parser.add_argument('--pkg-key', action='store', required=False)
    parser.add_argument('--fars-dir', action='store', required=False,
                        help="""Directory where intermediate files for the
                        package(s) will be stored""")
    parser.add_argument('--pkgs', action='append', required=False,
                        help="""Packages to publish. This argument may be
                        repeated to publish multiple packages.""")
    parser.add_argument('--quiet', action='store_true', required=False, default=False)
    parser.add_argument('--ver-scheme', action='store', required=False, default='mono',
                        help="""scheme to base repository versions on, defaults to "mono"
                        for "monotonic" but can be set to "time" to base versions on the number
                        of seconds since amber epoch""")
    parser.add_argument('--pkgs-dir', action='store', required=False,
                        help="""Parent directory of all generated package
                        metadata""")
    args = parser.parse_args()

    build_dir = args.build_dir

    host_tools_dir = args.host_tools_dir
    if not host_tools_dir:
      ptr_size = 8 * struct.calcsize("P")
      host_tools_dir = os.path.join(build_dir, "host_x%d" % ptr_size)
    pm_bin = os.path.join(host_tools_dir, "pm")
    if not os.path.exists(pm_bin):
        print "Could not find 'pm' tool at %s" % pm_bin
        return -1

    amber_bin = os.path.join(host_tools_dir, "amber-publish")
    if not os.path.exists(amber_bin):
        print "Could not find amber-publish tool at %s" % amber_bin
        return -1

    repo_dir = args.update_repo
    if not repo_dir:
        repo_dir = os.path.join(build_dir, "amber-files")
        if not os.path.exists(repo_dir):
          os.makedirs(repo_dir)

    if not os.path.exists(repo_dir):
        print "Publishing repository directory '%s' could not be found" % repo_dir
        return -1

    keys_src_dir = args.update_keys
    if not keys_src_dir:
        keys_src_dir = build_dir

    pkg_key = args.pkg_key
    if not pkg_key:
        pkg_key = os.path.join(build_dir, "pkg_key")
        result = gen_pkg_key(pm_bin, pkg_key)
        if result is not None:
            print result
            return -1

    pkg_stg_dir = args.fars_dir
    if not pkg_stg_dir:
        pkg_stg_dir = os.path.join(build_dir, "fars")
        if not os.path.exists(pkg_stg_dir):
          os.makedirs(pkg_stg_dir)

    if not os.path.exists(pkg_stg_dir):
        print "Packages staging directory '%s' could not be found" % pkg_stg_dir
        return -1

    pkg_list = args.pkgs
    if not pkg_list:
        pkg_list = []
        list_path = os.path.join(build_dir, "gen", "build", "gn", "packages")
        with open(list_path, "r") as pfile:
            for l in pfile:
                pkg_list.append(l.strip())

    if args.ver_scheme != "mono" and args.ver_scheme != "time":
      print "'%s' is not a recognized version scheme, use 'time' or 'mono'"
      return -1

    pkgs_dir = args.pkgs_dir
    if not pkgs_dir:
      pkgs_dir = os.path.join(build_dir, "package")

    return publish(pm_bin, amber_bin, pkg_key, keys_src_dir, pkg_stg_dir, repo_dir,
                   pkgs_dir, pkg_list, args.ver_scheme, not args.quiet)

if __name__ == '__main__':
    sys.exit(main())
