#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
build_all_fuchsia_packages.py drives the `pm` tool to build Fuchsia packages.

It accepts a list of package names as input and produces:
 * A signed meta.far for each package
 * A manifest file for pkgsvr that contains `package_name/package_version:merkleroot(meta.far)`
 * A manifest file listing all of the meta.fars and package content files (for input to blobfs(1))

TODO(US-415): move the behavior of finalize_manifests.py and
build_all_fuchsia_packages.py into package.gni to be performed on a
per-package basis as soon as package dependencies are available.
"""

import Queue
import argparse
import collections
import multiprocessing
import os
import subprocess
import sys
import threading

def package_build_worker(package_queue, blob_lines, pm, signing_key, packages_dir):
    while True:
        package = package_queue.get()
        if package is None:
            package_queue.task_done()
            return

        package_dir = os.path.join(packages_dir, package)
        package_manifest = os.path.join(package_dir, "final_package_manifest")

        subprocess.check_call([pm, "-k", signing_key, "-o", package_dir, "-m", package_manifest, "build"])

        # Parse the package file source map
        sources = {}
        with open(package_manifest) as s:
            for line in s:
                dst, src = line.strip().split("=", 2)
                if not dst.startswith("meta/"):
                    sources[dst] = src

        contents_manifest = os.path.join(package_dir, "meta", "contents")

        # For each blob the package needs, copy the source into the amber blobs tree
        with open(contents_manifest) as c:
            for line in c:
                dst, blob = line.strip().split("=", 2)
                blob_lines.append("%s=%s" % (sources[dst], blob))

        package_queue.task_done()

def main():
    parser = argparse.ArgumentParser("Generates a set of signed packages and associated output manifests")
    parser.add_argument("--package-list",
                        help="A list of package names to build",
                        required=True)
    parser.add_argument("--packages-dir",
                        help="The directory in which packages working files are found (manifests, meta.far, etc)",
                        required=True)
    parser.add_argument("--signing-key",
                        help="Path to the package signing key.",
                        required=True)
    parser.add_argument("--pm",
                        help="Path to the pm host binary used to build and sign the package meta.far.",
                        required=True)
    parser.add_argument("--merkleroot",
                        help="Path to the host merkleroot binary from the Zircon tools build",
                        required=True)
    parser.add_argument("--blob-manifest",
                        help="Path to the blob manifest to output (containing all produced meta.fars).",
                        required=True)
    # TODO(raggi): the pkgsvr-index should end up in a signed meta-package instead.
    parser.add_argument("--pkgsvr-index",
                        help="Path to the pkgsvr index to output (containing all package/version - merkleroot tuples).",
                        required=True)
    parser.add_argument("--amber-publish",
                        help="Path to the amber-publish tool.",
                        required=True)
    parser.add_argument("--amber-repo",
                        help="Path to the amber TUF repository.",
                        required=True)
    parser.add_argument("--amber-keys",
                        help="Path to the amber signing keys.",
                        required=True)
    parser.add_argument("--amber-package-list",
                        help="Path to the amber package list manifest to generate.",
                        required=True)
    parser.add_argument("--amber-blobs-manifest",
                        help="Path to the amber blobs manifest to generate.",
                        required=True)
    parser.add_argument("--concurrency",
                        help="Number of concurrent package manager processes to spawn",
                        default=multiprocessing.cpu_count(),
                        required=False)
    args = parser.parse_args()

    queue = Queue.Queue()
    # blob_lines collects all lines for writing to an amber-publish blobs manifest
    blob_lines = collections.deque()
    worker_args=[queue, blob_lines, args.pm, args.signing_key, args.packages_dir]
    threads = [threading.Thread(target=package_build_worker, args=worker_args) for _i in range(args.concurrency)]
    for thread in threads:
        thread.start()

    packages = []
    with open(args.package_list) as package_list:
        content = package_list.read().strip()
        for package in content.splitlines():
            package = package.strip()
            # Note: This is a bad heuristic really, as we can't produce empty packages, but GN is limiting in this regard.
            if os.path.getsize(os.path.join(args.packages_dir, package, "final_package_manifest")) > 0:
                queue.put(package)
                packages.append(package)

    for _i in range(args.concurrency):
        queue.put(None)

    for thread in threads:
        thread.join()

    # Note: Package versions will be utilized in the future
    package_version = 0

    with open(args.amber_package_list, "w+") as amber_package_list:
        for package in packages:
            meta_far = os.path.join(args.packages_dir, package, "meta.far")
            amber_package_list.write("%s/%s=%s\n" % (package, package_version, meta_far))
        if not packages:
            amber_package_list.write("")

    subprocess.check_call([args.amber_publish, "-ps", "-f", args.amber_package_list, "-r", args.amber_repo, "-k", args.amber_keys])

    with open(args.amber_blobs_manifest, "w+") as amber_blobs_manifest:
        for line in blob_lines:
            amber_blobs_manifest.write("%s\n" % line)

    subprocess.check_call([args.amber_publish, "-bs", "-f", args.amber_blobs_manifest, "-r", args.amber_repo, "-k", args.amber_keys])


    blob_manifest_dir = os.path.dirname(args.blob_manifest)
    with open(args.blob_manifest, "w+") as blob_manifest:
        with open(args.pkgsvr_index, "w+") as pkgsvr_index:
            for package in packages:
                meta_far = os.path.join(args.packages_dir, package, "meta.far")
                blob_manifest.write("%s\n" % os.path.relpath(meta_far, blob_manifest_dir))

                with open(os.path.join(args.packages_dir, package, "final_package_manifest")) as package_manifest:
                    for line in package_manifest:
                        # XXX: the blobfs tool always prepends the manifest
                        # directory to the input path, which means that we have
                        # to re-pack the line with a path relative to the
                        # manifest.
                        src = line.strip().split("=", 2)[1]
                        blob_manifest.write(os.path.relpath(src, blob_manifest_dir) + "\n")

                with open(meta_far + ".merkle") as merklefile:
                    pkgsvr_index.write("%s/%d=%s\n" % (package, package_version, merklefile.readline().strip()))
            if not packages:
                blob_manifest.write("")
                pkgsvr_index.write("")

    return 0

if __name__ == '__main__':
    sys.exit(main())
