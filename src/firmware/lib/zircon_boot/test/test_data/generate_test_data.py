#!/usr/bin/env python3.8

# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""The code is to provide a reference of how test_images.h is generated"""

import os
import tempfile
from typing import List
import subprocess

FUCHSIA_DIR = os.environ.get('FUCHSIA_DIR')
AVB_REPO = os.path.join(
    f'{FUCHSIA_DIR}', 'third_party', 'android', 'platform', 'external', 'avb')
AVB_TOOL = os.path.join(f'{AVB_REPO}', 'avbtool.py')
ZBI_TOOL = os.path.join(f'{FUCHSIA_DIR}', 'out', 'default', 'host_x64', 'zbi')
PSK = os.path.join(f'{AVB_REPO}', 'test', 'data', 'testkey_atx_psk.pem')
ATX_METADATA = os.path.join(f'{AVB_REPO}', 'test', 'data', 'atx_metadata.bin')
ATX_PERMANENT_ATTRIBUTES = os.path.join(
    f'{AVB_REPO}', 'test', 'data', 'atx_permanent_attributes.bin')

KERNEL_IMG_SIZE = 1024


def GetCArrayDeclaration(data: bytes, var_name: str) -> str:
    """Generates a C array declaration for a byte array"""
    decl = f'const unsigned char {var_name}[] =' + '{ '
    decl += " ".join([f'0x{b:x},' for b in data])
    decl += "};"
    return decl


def GenerateTestImageCArrayDeclaration(
        ab_suffix: str,
        rollback_index: int = 0,
        rollback_index_location: int = 0) -> List[str]:
    """Generates kernel/vbmeta image for a slot and their C array declarations """
    decls = []
    with tempfile.TemporaryDirectory() as temp_dir:
        kernel = f'{temp_dir}/test_zircon_{ab_suffix}.bin'
        kernel_zbi = f'{temp_dir}/test_zircon_{ab_suffix}.zbi'

        # Generate random kernel image
        with open(kernel, 'wb') as f:
            f.write(bytes(os.urandom(KERNEL_IMG_SIZE)))
        # Put image in a zbi container.
        subprocess.run(
            [
                ZBI_TOOL,
                '--output',
                kernel_zbi,
                '--type=KERNEL_ARM64',
                kernel,
            ])
        # Generate C array declaration
        with open(kernel_zbi, 'rb') as zbi:
            data = zbi.read()
            decls.append(
                GetCArrayDeclaration(
                    data, f'kTestZircon{ab_suffix.upper()}Image'))
        # Generate vbmeta descriptor.
        vbmeta_desc = f'{temp_dir}/test_vbmeta_{ab_suffix}.desc'
        subprocess.run(
            [
                AVB_TOOL,
                'add_hash_footer',
                '--image',
                kernel_zbi,
                '--partition_name',
                'zircon',
                '--do_not_append_vbmeta_image',
                '--output_vbmeta_image',
                vbmeta_desc,
                '--partition_size',
                '209715200',
            ])
        # Generate two cmdline ZBI items to add as property descriptors to vbmeta
        # image for test.
        vbmeta_prop_zbi_1 = f'{temp_dir}/test_vbmeta_prop_{ab_suffix}_1.img'
        subprocess.run(
            [
                ZBI_TOOL,
                '--output',
                vbmeta_prop_zbi_1,
                '--type=CMDLINE',
                f'--entry=vb_arg_1=foo_{ab_suffix}',
            ])
        vbmeta_prop_zbi_2 = f'{temp_dir}/test_vbmeta_prop_{ab_suffix}_2.img'
        subprocess.run(
            [
                ZBI_TOOL,
                '--output',
                vbmeta_prop_zbi_2,
                '--type=CMDLINE',
                f'--entry=vb_arg_2=bar_{ab_suffix}',
            ])
        # Generate vbmeta image
        vbmeta_img = f'{temp_dir}/test_vbmeta_{ab_suffix}.img'
        subprocess.run(
            [
                AVB_TOOL,
                'make_vbmeta_image',
                '--output',
                vbmeta_img,
                '--key',
                PSK,
                '--algorithm',
                'SHA512_RSA4096',
                '--public_key_metadata',
                ATX_METADATA,
                '--include_descriptors_from_image',
                vbmeta_desc,
                '--prop_from_file',
                f'zbi_vbmeta_arg_1:{vbmeta_prop_zbi_1}',
                '--prop_from_file',
                f'zbi_vbmeta_arg_2:{vbmeta_prop_zbi_2}',
                # The following should not be recognized as vbmeta zbi item, as
                # the value is not a zbi item.
                '--prop',
                f'zbi_vbmeta_arg_3:abc',
                '--rollback_index',
                f'{rollback_index}',
                '--rollback_index_location',
                f'{rollback_index_location}',
            ])
        # Generate C array declaration
        with open(vbmeta_img, 'rb') as vbmeta:
            data = vbmeta.read()
            decls.append(
                GetCArrayDeclaration(
                    data, f'kTestVbmeta{ab_suffix.upper()}Image'))
    return decls


def GeneratePermanentAttributesDeclaration(file_name: str) -> str:
    with open(file_name, 'rb') as perm_attr:
        data = perm_attr.read()
        return GetCArrayDeclaration(data, 'kPermanentAttributes')


def GenerateVariableDeclarationHeader(decls: List[str], out_file: str):
    """Writes a header file that contains all C array declarations"""
    with open(out_file, 'w') as f:
        f.write(
            """// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

        """)
        # This will trigger 'fx format-code' to fix inclusion macro.
        f.write('#pragma once\n\n')
        for decl in decls:
            f.write(f'{decl}\n\n')
    subprocess.run(['fx', 'format-code', f'--files={out_file}'])


if __name__ == '__main__':
    decls = []
    # slot images
    decls.extend(GenerateTestImageCArrayDeclaration('a', rollback_index=5))
    decls.extend(GenerateTestImageCArrayDeclaration('b', rollback_index=10))
    decls.extend(GenerateTestImageCArrayDeclaration('r'))
    # permanent attributes
    decls.append(
        GeneratePermanentAttributesDeclaration(ATX_PERMANENT_ATTRIBUTES))
    GenerateVariableDeclarationHeader(decls, 'test_images.h')
