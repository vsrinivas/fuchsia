#!/usr/bin/env python3
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Run unit-test suite for compare_toolchains.py."""

import os
import sys
import unittest

import compare_toolchains as ct

_TEST_TOOLCHAIN_NINJA_TEXT = r'''
rule efi-x64-win-clang_alink
  command = rm -f ${out} && ../../prebuilt/third_party/clang/linux-x64/bin/llvm-ar ${arflags} cqsD ${out} '@${out}.rsp'
  description = AR ${out}
  rspfile = ${out}.rsp
  rspfile_content = ${in}
rule efi-x64-win-clang_stamp
  command = touch ${out}
  description = STAMP ${out}
rule efi-x64-win-clang_link
  command = ../../prebuilt/third_party/clang/linux-x64/bin/clang++ -o ${output_dir}/${target_output_name}${output_extension} ${ldflags} @'${output_dir}/${target_output_name}.rsp' ${libs} ${solibs}
  description = LINK ${output_dir}/${target_output_name}${output_extension}
  rspfile = ${output_dir}/${target_output_name}.rsp
  rspfile_content = ${in}
rule efi-x64-win-clang_cxx
  command = ../../prebuilt/third_party/clang/linux-x64/bin/clang++ -MD -MF ${out}.d -o ${out} ${defines} ${include_dirs} ${cflags} ${cflags_cc} -c ${in}
  description = CXX ${out}
  depfile = ${out}.d
  deps = gcc
rule efi-x64-win-clang_cc
  command = ../../prebuilt/third_party/clang/linux-x64/bin/clang -MD -MF ${out}.d -o ${out} ${defines} ${include_dirs} ${cflags} ${cflags_c} -c ${in}
  description = CC ${out}
  depfile = ${out}.d
  deps = gcc
rule efi-x64-win-clang_asm
  command = ../../prebuilt/third_party/clang/linux-x64/bin/clang -MD -MF ${out}.d -o ${out} ${defines} ${include_dirs} ${asmflags} -c ${in}
  description = ASM ${out}
  depfile = ${out}.d
  deps = gcc
rule efi-x64-win-clang_copy
  command = ln -f ${in} ${out}
  description = COPY ${in} ${out}

build efi-x64-win-clang/obj/bootloader/bootloader.stamp: efi-x64-win-clang_stamp efi-x64-win-clang/obj/bootloader/bootloader.binary.stamp efi-x64-win-clang/obj
/bootloader/bootloader.manifest.stamp
build efi-x64-win-clang/obj/bootloader/bootloader.binary.stamp: efi-x64-win-clang_stamp efi-x64-win-clang/bootx64.efi
subninja efi-x64-win-clang/obj/bootloader/bootloader.binary._build.ninja
......
'''


class TestParseToolchainNinjaFile(unittest.TestCase):

    def test_simple_rule_definition(self):
        toolchain_ninja = r'''# This comment shall be ignored
# As well as empty lines below

rule test-toolchain_alink
  command = blah -o ${out} --blarg '@${out}.rsp'
  description = Do blah
  rspfile = ${out}.rspfile
  rspfile_content = ${in}


'''
        expected = {
            'alink':
                {
                    'command': "blah -o ${out} --blarg '@${out}.rsp'",
                    'description': 'Do blah',
                    'rspfile': '${out}.rspfile',
                    'rspfile_content': '${in}',
                }
        }

        self.assertDictEqual(
            ct.parse_toolchain_ninja_file(toolchain_ninja, 'test-toolchain'),
            expected)

    def test_multiple_rule_definitions(self):
        expected = {
            'alink':
                {
                    'command':
                        "rm -f ${out} && ../../prebuilt/third_party/clang/linux-x64/bin/llvm-ar ${arflags} cqsD ${out} '@${out}.rsp'",
                    'description':
                        'AR ${out}',
                    'rspfile':
                        '${out}.rsp',
                    'rspfile_content':
                        '${in}',
                },
            'link':
                {
                    'command':
                        "../../prebuilt/third_party/clang/linux-x64/bin/clang++ -o ${output_dir}/${target_output_name}${output_extension} ${ldflags} @'${output_dir}/${target_output_name}.rsp' ${libs} ${solibs}",
                    'description':
                        'LINK ${output_dir}/${target_output_name}${output_extension}',
                    'rspfile':
                        '${output_dir}/${target_output_name}.rsp',
                    'rspfile_content':
                        '${in}',
                },
            'cc':
                {
                    'command':
                        '../../prebuilt/third_party/clang/linux-x64/bin/clang -MD -MF ${out}.d -o ${out} ${defines} ${include_dirs} ${cflags} ${cflags_c} -c ${in}',
                    'description':
                        'CC ${out}',
                    'depfile':
                        '${out}.d',
                    'deps':
                        'gcc',
                },
            'cxx':
                {
                    'command':
                        '../../prebuilt/third_party/clang/linux-x64/bin/clang++ -MD -MF ${out}.d -o ${out} ${defines} ${include_dirs} ${cflags} ${cflags_cc} -c ${in}',
                    'description':
                        'CXX ${out}',
                    'depfile':
                        '${out}.d',
                    'deps':
                        'gcc',
                },
            'asm':
                {
                    'command':
                        '../../prebuilt/third_party/clang/linux-x64/bin/clang -MD -MF ${out}.d -o ${out} ${defines} ${include_dirs} ${asmflags} -c ${in}',
                    'description':
                        'ASM ${out}',
                    'depfile':
                        '${out}.d',
                    'deps':
                        'gcc',
                },
            'copy':
                {
                    'command': 'ln -f ${in} ${out}',
                    'description': 'COPY ${in} ${out}',
                },
            'stamp': {
                'command': 'touch ${out}',
                'description': 'STAMP ${out}',
            },
        }
        self.maxDiff = None
        self.assertDictEqual(
            ct.parse_toolchain_ninja_file(
                _TEST_TOOLCHAIN_NINJA_TEXT, 'efi-x64-win-clang'), expected)


class TestPrettyPrintCommandsList(unittest.TestCase):

    def test_simple_commands(self):
        input_cmds = [
            'copy foo bar',
            'rm -f foo.debug && ln -sf lib.unstripped/foo foo.debug && touch foo.stamp',
        ]

        expected = r'''copy foo bar
rm -f foo.debug && \
    ln -sf lib.unstripped/foo foo.debug && \
    touch foo.stamp
'''
        self.assertEqual(ct.pretty_print_commands_list(input_cmds), expected)

    def test_compile_commands(self):
        input_cmds = [
            '.../clang++ -o program -shared main.cc libfoo.a -lbar && touch program.stamp',
        ]
        expected = r'''.../clang++ \
    -o \
    program \
    -shared \
    main.cc \
    libfoo.a \
    -lbar \
    && \
    touch \
    program.stamp
'''
        self.assertEqual(ct.pretty_print_commands_list(input_cmds), expected)


if __name__ == "__main__":
    unittest.main()
