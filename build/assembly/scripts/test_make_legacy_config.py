# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""!/usr/bin/env python3.8"""

import functools
import hashlib
import os
import tempfile
from typing import Callable, List, Tuple
import unittest

from assembly import FilePath, FileEntry, ImageAssemblyConfig, PackageManifest, BlobEntry, PackageMetaData
import make_legacy_config
import serialization


def fast_copy_mock(
        src: FilePath, dst: FilePath, tracked_copies: List[FileEntry]) -> None:
    """A bindable-mock of assembly.fast_copy() that tracks all of the copies
    that it's asked to perform in the passed-in list.
    """
    tracked_copies.append(FileEntry(source_path=src, dest_path=dst))


def create_fast_copy_mock_instance() -> Tuple[Callable, List[FileEntry]]:
    """Create a mock implementation of fast_copy() that's bound to a list of
    FileEntries in which it records all the copies it's asked to make.
    """
    copies = []
    return (functools.partial(fast_copy_mock, tracked_copies=copies), copies)


def make_merkle(blob_name: str) -> str:
    """Creates a "merkle" by hashing the blob_name to get a unique value.
    """
    m = hashlib.sha256()
    m.update(blob_name.encode("utf-8"))
    return m.hexdigest()


class MakeLegacyConfig(unittest.TestCase):

    def test_make_legacy_config(self):
        self.maxDiff = None

        # Patch in a mock for the fast_copy() fn
        fast_copy_mock_fn, copies = create_fast_copy_mock_instance()
        make_legacy_config.fast_copy = fast_copy_mock_fn

        with tempfile.TemporaryDirectory() as temp_dir_path:
            os.chdir(temp_dir_path)

            source_dir = "source"
            os.mkdir(source_dir)

            # Create an ImageAssembly configuration
            image_assembly = ImageAssemblyConfig()

            # Write out package manifests which are part of the package.
            for package_set in ["base", "cache", "system"]:
                for suffix in ["a", "b"]:
                    package_name = f"{package_set}_{suffix}"
                    manifest = PackageManifest(
                        PackageMetaData(package_name), [])

                    # Create a few blob entries (that don't need to fully exist)
                    for blob_suffix in ["1", "2", "3"]:
                        blob_name = f"internal/path/file_{suffix}_{blob_suffix}"
                        entry = BlobEntry(
                            blob_name, make_merkle(package_name + blob_name),
                            None,
                            os.path.join(source_dir, package_name, blob_name))
                        manifest.blobs.append(entry)

                    # Write the manifest out to the temp dir
                    manifest_path = os.path.join(
                        source_dir, f"{package_name}.json")
                    with open(manifest_path, 'w') as manifest_file:
                        serialization.json_dump(
                            manifest, manifest_file, indent=2)

                    # Add to the ImageAssembly in the correct package set.
                    getattr(image_assembly, package_set).add(manifest_path)

            # Add the rest of the fields we expect to see in an image_assembly
            # config.
            image_assembly.boot_args.update(["boot-arg-1", "boot-arg-2"])
            image_assembly.kernel.path = os.path.join(source_dir, "kernel.bin")
            image_assembly.kernel.args.update(["arg1", "arg2"])
            image_assembly.kernel.clock_backstop = "123456"
            image_assembly.bootfs_files.update(
                [
                    FileEntry(
                        os.path.join(source_dir, "some/file"), "some/file"),
                    FileEntry(
                        os.path.join(source_dir, "another/file"),
                        "another/file"),
                ])

            # Create the outdir path, and perform the "copying" into the
            # AssemblyInputBundle.
            outdir = "outdir"
            aib, deps = make_legacy_config.copy_to_assembly_input_bundle(
                image_assembly, [], outdir)

            # Validate the contents of the AssemblyInputBundle itself
            self.assertEquals(
                aib.base, set(["packages/base/base_a", "packages/base/base_b"]))
            self.assertEquals(
                aib.cache,
                set(["packages/cache/cache_a", "packages/cache/cache_b"]))
            self.assertEquals(
                aib.system,
                set(["packages/system/system_a", "packages/system/system_b"]))
            self.assertEquals(aib.boot_args, set(["boot-arg-1", "boot-arg-2"]))
            self.assertEquals(aib.kernel.path, "kernel/kernel.bin")
            self.assertEquals(aib.kernel.args, set(["arg1", "arg2"]))
            self.assertEquals(aib.kernel.clock_backstop, "123456")
            self.assertEquals(
                aib.bootfs_files,
                set(
                    [
                        FileEntry(
                            source_path="bootfs/some/file",
                            dest_path="some/file"),
                        FileEntry(
                            source_path="bootfs/another/file",
                            dest_path="another/file"),
                    ]))

            # Make sure all the manifests were created in the correct location.
            for package_set in ["base", "cache", "system"]:
                for suffix in ["a", "b"]:
                    package_name = f"{package_set}_{suffix}"
                    with open(f"outdir/packages/{package_set}/{package_name}"
                             ) as manifest_file:
                        manifest = serialization.json_load(
                            PackageManifest, manifest_file)
                        self.assertEquals(manifest.package.name, package_name)
                        self.assertEquals(
                            set(manifest.blobs_by_path().keys()),
                            set(
                                [
                                    f'internal/path/file_{suffix}_1',
                                    f'internal/path/file_{suffix}_2',
                                    f'internal/path/file_{suffix}_3',
                                ]))

            # Spot-check one of the manifests, that it contains the correct
            # source paths to the blobs.
            with open("outdir/packages/base/base_a") as manifest_file:
                manifest = serialization.json_load(
                    PackageManifest, manifest_file)
                self.assertEquals(manifest.package.name, "base_a")
                self.assertEquals(len(manifest.blobs), 3)
                blobs = manifest.blobs_by_path()
                self.assertEquals(
                    blobs['internal/path/file_a_1'].source_path,
                    '../../blobs/efac096092f7cf879c72ac51d23d9f142e97405dec7dd9c69aeee81de083f794'
                )
                self.assertEquals(
                    blobs['internal/path/file_a_1'].merkle,
                    'efac096092f7cf879c72ac51d23d9f142e97405dec7dd9c69aeee81de083f794'
                )
                self.assertEquals(
                    blobs['internal/path/file_a_2'].source_path,
                    '../../blobs/bf0c3ae1356b5863258f73a37d555cf878007b8bfe4fd780d74466ec62fe062d'
                )
                self.assertEquals(
                    blobs['internal/path/file_a_2'].merkle,
                    'bf0c3ae1356b5863258f73a37d555cf878007b8bfe4fd780d74466ec62fe062d'
                )
                self.assertEquals(
                    blobs['internal/path/file_a_3'].source_path,
                    '../../blobs/a2e574ccd55c815f0a87c4f27e7a3115fe8e46d41a2e0caf2a91096a41421f78'
                )
                self.assertEquals(
                    blobs['internal/path/file_a_3'].merkle,
                    'a2e574ccd55c815f0a87c4f27e7a3115fe8e46d41a2e0caf2a91096a41421f78'
                )

            # Validate that the deps were correctly identified (all package
            # manifest paths, the blob source paths, the bootfs source paths,
            # and the kernel source path)
            self.assertEquals(
                deps,
                set(
                    [
                        'source/base_a.json',
                        'source/base_a/internal/path/file_a_1',
                        'source/base_a/internal/path/file_a_2',
                        'source/base_a/internal/path/file_a_3',
                        'source/base_b.json',
                        'source/base_b/internal/path/file_b_1',
                        'source/base_b/internal/path/file_b_2',
                        'source/base_b/internal/path/file_b_3',
                        'source/cache_a.json',
                        'source/cache_a/internal/path/file_a_1',
                        'source/cache_a/internal/path/file_a_2',
                        'source/cache_a/internal/path/file_a_3',
                        'source/cache_b.json',
                        'source/cache_b/internal/path/file_b_1',
                        'source/cache_b/internal/path/file_b_2',
                        'source/cache_b/internal/path/file_b_3',
                        'source/system_a.json',
                        'source/system_a/internal/path/file_a_1',
                        'source/system_a/internal/path/file_a_2',
                        'source/system_a/internal/path/file_a_3',
                        'source/system_b.json',
                        'source/system_b/internal/path/file_b_1',
                        'source/system_b/internal/path/file_b_2',
                        'source/system_b/internal/path/file_b_3',
                        'source/kernel.bin', 'source/some/file',
                        'source/another/file'
                    ]))

            # Validate that all the files were correctly copied to the
            # correct paths in the AIB.
            self.assertEquals(
                set(copies),
                set(
                    [
                        FileEntry(
                            source_path='source/base_a/internal/path/file_a_1',
                            dest_path=
                            'outdir/blobs/efac096092f7cf879c72ac51d23d9f142e97405dec7dd9c69aeee81de083f794'
                        ),
                        FileEntry(
                            source_path='source/base_a/internal/path/file_a_1',
                            dest_path=
                            'outdir/blobs/efac096092f7cf879c72ac51d23d9f142e97405dec7dd9c69aeee81de083f794'
                        ),
                        FileEntry(
                            source_path='source/base_a/internal/path/file_a_2',
                            dest_path=
                            'outdir/blobs/bf0c3ae1356b5863258f73a37d555cf878007b8bfe4fd780d74466ec62fe062d'
                        ),
                        FileEntry(
                            source_path='source/base_a/internal/path/file_a_3',
                            dest_path=
                            'outdir/blobs/a2e574ccd55c815f0a87c4f27e7a3115fe8e46d41a2e0caf2a91096a41421f78'
                        ),
                        FileEntry(
                            source_path='source/base_b/internal/path/file_b_1',
                            dest_path=
                            'outdir/blobs/ae9fd81e1c2fd1b084ec2c362737e812c5ef9b3aa8cb0538ec8e2269ea7fbe1a'
                        ),
                        FileEntry(
                            source_path='source/base_b/internal/path/file_b_2',
                            dest_path=
                            'outdir/blobs/d3cd38c4881c3bc31f1e2e397a548d431a6430299785446f28be10cc5b76d92b'
                        ),
                        FileEntry(
                            source_path='source/base_b/internal/path/file_b_3',
                            dest_path=
                            'outdir/blobs/6468d9d6761c8afcc97744dfd9e066f29bb697a9a0c8248b5e6eec989134a048'
                        ),
                        FileEntry(
                            source_path='source/cache_a/internal/path/file_a_1',
                            dest_path=
                            'outdir/blobs/f0601d51be1ec8c11d825b756841937706eb2805ce9b924b67b4b0dc14caba29'
                        ),
                        FileEntry(
                            source_path='source/cache_a/internal/path/file_a_2',
                            dest_path=
                            'outdir/blobs/1834109a42a5ff6501fbe05216475b2b0acc44e0d9c94924469a485d6f45dc86'
                        ),
                        FileEntry(
                            source_path='source/cache_a/internal/path/file_a_3',
                            dest_path=
                            'outdir/blobs/0f32059964674afd810001c76c2a5d783a2ce012c41303685ec1adfdb83290fd'
                        ),
                        FileEntry(
                            source_path='source/cache_b/internal/path/file_b_1',
                            dest_path=
                            'outdir/blobs/301e8584305e63f0b764daf52dcf312eecb6378b201663fcc77d7ad68aab1f23'
                        ),
                        FileEntry(
                            source_path='source/cache_b/internal/path/file_b_2',
                            dest_path=
                            'outdir/blobs/8135016519df51d386efaea9b02f50cb454b6c7afe69c77895c1d4d844c3584d'
                        ),
                        FileEntry(
                            source_path='source/cache_b/internal/path/file_b_3',
                            dest_path=
                            'outdir/blobs/b548948fd2dc40574775308a92a8330e5c5d84ddf31513d1fe69964b458479e7'
                        ),
                        FileEntry(
                            source_path='source/system_a/internal/path/file_a_1',
                            dest_path=
                            'outdir/blobs/8ca898b1389c58b6cd9a6a777e320f2756ab3437b402c61d774dd2758ad9cf06'
                        ),
                        FileEntry(
                            source_path='source/system_a/internal/path/file_a_2',
                            dest_path=
                            'outdir/blobs/ef84c6711eaba482164fe4eb08a6c45f18fe62d493e5a31a631c32937bf7229d'
                        ),
                        FileEntry(
                            source_path='source/system_a/internal/path/file_a_3',
                            dest_path=
                            'outdir/blobs/d66cb673257e25393a319fb2c3e9745ef6e0f1cfa4fb89c5576df73cd3eba586'
                        ),
                        FileEntry(
                            source_path='source/system_b/internal/path/file_b_1',
                            dest_path=
                            'outdir/blobs/fd0891d15ce65d7682f7437e441e917b8ed4bde4db07a11dc100104f25056051'
                        ),
                        FileEntry(
                            source_path='source/system_b/internal/path/file_b_2',
                            dest_path=
                            'outdir/blobs/c244c7c6ebf40a9a4c9d59e7b08a1cf54ae3d60404d1cecb417a7b55cc308d91'
                        ),
                        FileEntry(
                            source_path='source/system_b/internal/path/file_b_3',
                            dest_path=
                            'outdir/blobs/0cdbf3e4f1246ce7522e78c21bcf1c3aef2d41ac2b4de3f0ee98fc6273f62eb9'
                        ),
                        FileEntry(
                            source_path='source/kernel.bin',
                            dest_path='outdir/kernel/kernel.bin'),
                        FileEntry(
                            source_path='source/some/file',
                            dest_path='outdir/bootfs/some/file'),
                        FileEntry(
                            source_path='source/another/file',
                            dest_path='outdir/bootfs/another/file'),
                    ]))
