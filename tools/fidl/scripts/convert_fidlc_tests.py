#!/usr/bin/env python3.8
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file

import os
import re
import subprocess
import tempfile

FUCHSIA_DIR = os.environ['FUCHSIA_DIR']
TEST_DIR = 'zircon/system/utest/fidl-compiler'
EXPERIMENTAL_FLAG_INIT = '  fidl::ExperimentalFlags experimental_flags;\n  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);\n'
EXPERIMENTAL_FLAG_ARG = ',\n                      experimental_flags'


def convert_fidl(old: str) -> str:
    with tempfile.NamedTemporaryFile(mode='w', suffix='.fidl') as input_f:
        input_f.write(old)
        input_f.flush()
        with tempfile.TemporaryDirectory() as out_dir_name:
            args = [
                'out/default/host_x64-asan/exe.unstripped/fidlc',
                '--experimental', 'old_syntax_only', '--convert-syntax',
                out_dir_name, '--files', input_f.name
            ]
            subprocess.run(args)
            files = os.listdir(out_dir_name)
            assert len(files) == 1
            return open(os.path.join(out_dir_name, files[0]), 'r').read()


def convert_file(test_file: str):
    contents = open(os.path.join(FUCHSIA_DIR, TEST_DIR, test_file), 'r').read()
    errors = []
    for test in get_test_bodies(contents):
        test = test[test.find('{'):]
        if 'ASSERT_COMPILED_AND_CONVERT' in test:
            converted_test = test.replace(
                'ASSERT_COMPILED_AND_CONVERT', 'ASSERT_COMPILED')
            for match in re.findall(r'(?s)(?<=R\"FIDL\().*?(?=\)FIDL\")', test):
                try:
                    converted = convert_fidl(match)
                except:
                    errors.append(match)
                else:
                    converted_test = converted_test.replace(match, converted)
            contents = contents.replace(test, converted_test)
        else:
            updated_test = test.replace(EXPERIMENTAL_FLAG_INIT,
                                        '').replace(EXPERIMENTAL_FLAG_ARG, '')
            contents = contents.replace(test, updated_test)
    open(os.path.join(FUCHSIA_DIR, TEST_DIR, test_file), 'w').write(contents)

    if errors:
        print(f'errors for {test_file}:')
        for e in errors:
            print(f'\t{e}')


def remove_old_only_tests(test_file: str):
    contents = open(os.path.join(FUCHSIA_DIR, TEST_DIR, test_file), 'r').read()
    for test in get_test_bodies(contents):
        if 'Old)' in test:
            contents = contents.replace(f'\n{test}\n', '')
    open(os.path.join(FUCHSIA_DIR, TEST_DIR, test_file), 'w').write(contents)


def get_test_bodies(test_file: str):
    lines = test_file.split('\n')
    next_test = []
    in_test = False
    for line in lines:
        if line.startswith('TEST('):
            in_test = True

        if in_test:
            next_test.append(line)

        if line == '}':
            yield '\n'.join(next_test)
            in_test = False
            next_test = []
            continue


if __name__ == '__main__':
    # print(convert_fidl('library test;\nstruct Foo {};\n'))
    all_tests = [
        'declaration_order_tests.cc',
        'recoverable_parsing_tests.cc',
        'span_tests.cc',
        'coded_types_generator_tests.cc',
        'recursion_detector_tests.cc',
        # 'new_syntax_tests.cc',
        'resource_tests.cc',
        'consts_tests.cc',
        'service_tests.cc',
        'array_tests.cc',
        'alias_tests.cc',
        'flexible_tests.cc',
        'virtual_source_tests.cc',
        'lint_findings_tests.cc',
        'handle_tests.cc',
        'strictness_tests.cc',
        'parsing_tests.cc',
        'protocol_tests.cc',
        'c_generator_tests.cc',
        'types_tests.cc',
        'structs_tests.cc',
        'direct_dependencies_tests.cc',
        # 'new_syntax_converter_tests.cc',
        'json_diagnostics_tests.cc',
        'utils_tests.cc',
        'resourceness_tests.cc',
        'errors_tests.cc',
        'union_tests.cc',
        'enums_tests.cc',
        'json_findings_tests.cc',
        'canonical_names_tests.cc',
        'lint_tests.cc',
        'table_tests.cc',
        'reporter_tests.cc',
        'visitor_unittests.cc',
        'recoverable_compilation_tests.cc',
        'bits_tests.cc',
        'flat_ast_tests.cc',
        'using_tests.cc',
        'typeshape_tests.cc',
        'attributes_tests.cc',
        'ordinals_tests.cc',
        'new_formatter_tests.cc'
    ]
    convert_file('new_syntax_tests.cc')
    # remove_old_only_tests('alias_tests.cc')
    # for test in all_tests:
    # convert_file(test)
    # remove_old_only_tests(test)
