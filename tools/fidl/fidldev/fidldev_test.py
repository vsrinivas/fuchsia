import itertools
import os
import unittest

from test_util import get_commands, MOCK_FUCHSIA_DIR, MOCK_BUILD_DIR
import regen
import util


# Many mocks contain 3 return values, because the regen script can call
# get_changed_files up to 3 times:
#   1. get the initial set of changed files
#   2. get changed files after fidlc regen, to check if fidlgen needs regen
#   3. get changed files after fidlgen regen, to check if go needs regen
class TestFidlDevRegen(unittest.TestCase):

    def test_basic_regen(self):
        mocks = {
            'get_changed_files':
                itertools.repeat(['zircon/tools/fidl/lib/flat_ast.cc']),
        }
        command = ['regen']
        expected = [
            util.BUILD_FIDLC,
            regen.path_to_regen_command(regen.FIDLC_REGEN),
        ]
        self.assertListEqual(get_commands(mocks, command), expected)

    def test_ir_changed(self):
        mocks = {
            'get_changed_files':
                [
                    ['zircon/tools/fidl/lib/parser.cc'],
                    [
                        'zircon/tools/fidl/lib/parser.cc',
                        'zircon/tools/fidl/goldens/bits.test.json.golden'
                    ],
                    [
                        'zircon/tools/fidl/lib/parser.cc',
                        'zircon/tools/fidl/goldens/bits.test.json.golden'
                    ],
                ]
        }
        command = ['regen']
        expected = [
            util.BUILD_FIDLC,
            regen.path_to_regen_command(regen.FIDLC_REGEN), util.BUILD_FIDLGEN,
            regen.path_to_regen_command(regen.FIDLGEN_REGEN),
            util.BUILD_FIDLGEN_DART,
            regen.path_to_regen_command(regen.FIDLGEN_DART_REGEN)
        ]
        self.assertListEqual(get_commands(mocks, command), expected)

    def test_tables_changed(self):
        mocks = {
            'get_changed_files':
                [
                    ['zircon/tools/fidl/lib/parser.cc'],
                    [
                        'zircon/tools/fidl/lib/parser.cc',
                        'zircon/tools/fidl/goldens/bits.test.tables.c.golden'
                    ],
                    [
                        'zircon/tools/fidl/lib/parser.cc',
                        'zircon/tools/fidl/goldens/bits.test.tables.c.golden'
                    ],
                ]
        }
        command = ['regen']
        expected = [
            util.BUILD_FIDLC,
            regen.path_to_regen_command(regen.FIDLC_REGEN),
        ]
        self.assertListEqual(get_commands(mocks, command), expected)

    def test_go_goldens_changed(self):
        mocks = {
            'get_changed_files':
                [
                    ['zircon/tools/fidl/lib/parser.cc'],
                    [
                        'zircon/tools/fidl/lib/parser.cc',
                        'zircon/tools/fidl/goldens/bits.test.json.golden'
                    ],
                    [
                        'zircon/tools/fidl/lib/parser.cc',
                        'zircon/tools/fidl/goldens/bits.test.json.golden',
                        'garnet/go/src/fidl/compiler/backend/goldens/union.test.json.go.golden'
                    ],
                ]
        }
        command = ['regen']
        expected = [
            util.BUILD_FIDLC,
            regen.path_to_regen_command(regen.FIDLC_REGEN),
            util.BUILD_FIDLGEN,
            regen.path_to_regen_command(regen.FIDLGEN_REGEN),
            util.BUILD_FIDLGEN_DART,
            regen.path_to_regen_command(regen.FIDLGEN_DART_REGEN),
            regen.path_to_regen_command(regen.GO_BINDINGS_REGEN),
        ]
        self.assertListEqual(get_commands(mocks, command), expected)

    def test_rust_goldens_changed(self):
        mocks = {
            'get_changed_files':
                [
                    ['zircon/tools/fidl/lib/parser.cc'],
                    [
                        'zircon/tools/fidl/lib/parser.cc',
                        'zircon/tools/fidl/goldens/bits.test.json.golden'
                    ],
                    [
                        'zircon/tools/fidl/lib/parser.cc',
                        'zircon/tools/fidl/goldens/bits.test.json.golden',
                        'garnet/go/src/fidl/compiler/backend/goldens/union.test.json.rs.golden'
                    ],
                ]
        }
        command = ['regen']
        expected = [
            util.BUILD_FIDLC,
            regen.path_to_regen_command(regen.FIDLC_REGEN),
            util.BUILD_FIDLGEN,
            regen.path_to_regen_command(regen.FIDLGEN_REGEN),
            util.BUILD_FIDLGEN_DART,
            regen.path_to_regen_command(regen.FIDLGEN_DART_REGEN),
        ]
        self.assertListEqual(get_commands(mocks, command), expected)

    def test_fidlgen_go_changed(self):
        mocks = {
            'get_changed_files':
                itertools.repeat(['tools/fidl/fidlgen_go/ir/ir.go'])
        }
        command = ['regen']
        expected = [
            util.BUILD_FIDLGEN,
            regen.path_to_regen_command(regen.FIDLGEN_REGEN),
            regen.path_to_regen_command(regen.GO_BINDINGS_REGEN),
        ]
        self.assertListEqual(get_commands(mocks, command), expected)

    def test_fidlgen_changed(self):
        mocks = {
            'get_changed_files':
                itertools.repeat(
                    ['tools/fidl/fidlgen_syzkaller/templates/struct.tmpl.go'])
        }
        command = ['regen']
        expected = [
            util.BUILD_FIDLGEN,
            regen.path_to_regen_command(regen.FIDLGEN_REGEN),
        ]
        self.assertListEqual(get_commands(mocks, command), expected)

    def test_fidlgen_dart_changed(self):
        mocks = {
            'get_changed_files':
                itertools.repeat(['topaz/bin/fidlgen_dart/fidlgen_dart.go'])
        }
        command = ['regen']
        expected = [
            util.BUILD_FIDLGEN_DART,
            regen.path_to_regen_command(regen.FIDLGEN_DART_REGEN),
        ]
        self.assertListEqual(get_commands(mocks, command), expected)

    def test_regen_all(self):
        command = ['regen', 'all']
        expected = [
            util.BUILD_FIDLC,
            regen.path_to_regen_command(regen.FIDLC_REGEN),
            util.BUILD_FIDLGEN,
            regen.path_to_regen_command(regen.FIDLGEN_REGEN),
            util.BUILD_FIDLGEN_DART,
            regen.path_to_regen_command(regen.FIDLGEN_DART_REGEN),
            regen.path_to_regen_command(regen.GO_BINDINGS_REGEN),
        ]
        self.assertListEqual(get_commands({}, command), expected)

    def test_regen_no_build(self):
        command = ['regen', 'all', '--no-build']
        expected = [
            regen.path_to_regen_command(regen.FIDLC_REGEN),
            regen.path_to_regen_command(regen.FIDLGEN_REGEN),
            regen.path_to_regen_command(regen.FIDLGEN_DART_REGEN),
            regen.path_to_regen_command(regen.GO_BINDINGS_REGEN),
        ]
        self.assertListEqual(get_commands({}, command), expected)

    def test_regen_fidlgen(self):
        command = ['regen', 'fidlgen']
        expected = [
            util.BUILD_FIDLGEN,
            regen.path_to_regen_command(regen.FIDLGEN_REGEN),
        ]
        self.assertListEqual(get_commands({}, command), expected)


class TestFidlDevTest(unittest.TestCase):

    def test_no_changes(self):
        mocks = {'get_changed_files': [[]]}
        command = ['test', '--no-regen']
        self.assertListEqual(get_commands(mocks, command), [])

    def test_fidlc_changed(self):
        mocks = {'get_changed_files': [['zircon/tools/fidl/lib/parser.cc']]}
        command = ['test', '--no-regen']
        expected = [util.BUILD_FIDLC_TESTS, util.TEST_FIDLC]
        self.assertListEqual(get_commands(mocks, command), expected)

    def test_ir_changed_zircon(self):
        mocks = {
            'get_changed_files':
                [['zircon/tools/fidl/goldens/bits.test.json.golden']]
        }
        command = ['test', '--no-regen']
        expected = [util.BUILD_FIDLC_TESTS, util.TEST_FIDLC]
        self.assertListEqual(get_commands(mocks, command), expected)

    def test_ir_changed(self):
        mocks = {
            'get_changed_files':
                [
                    [
                        'garnet/go/src/fidl/compiler/backend/goldens/struct.test.json',
                    ]
                ]
        }
        command = ['test', '--no-regen']
        actual = get_commands(mocks, command)
        expected = set(util.FIDLGEN_TEST_TARGETS)
        self.assertEqual(len(actual), 2)
        self.assertEqual(actual[0], util.BUILD_FIDLGEN)
        self.assertTestsRun(actual[1], expected)

    def test_fidlgen_util_changed(self):
        mocks = {
            'get_changed_files':
                [[
                    'garnet/go/src/fidl/compiler/backend/types/types.go',
                ]]
        }
        command = ['test', '--no-regen']
        actual = get_commands(mocks, command)
        expected = set(util.FIDLGEN_TEST_TARGETS)
        self.assertEqual(len(actual), 2)
        self.assertEqual(actual[0], util.BUILD_FIDLGEN)
        self.assertTestsRun(actual[1], expected)

    def test_fidlgen_backend_changed(self):
        mocks = {
            'get_changed_files':
                [[
                    'tools/fidl/fidlgen_rust/templates/enum.tmpl.go',
                ]]
        }
        command = ['test', '--no-regen']
        actual = get_commands(mocks, command)
        expected = set(util.FIDLGEN_TEST_TARGETS)
        self.assertEqual(len(actual), 2)
        self.assertEqual(actual[0], util.BUILD_FIDLGEN)
        self.assertTestsRun(actual[1], expected)

    def test_fidlgen_golden_changed(self):
        mocks = {
            'get_changed_files':
                [
                    [
                        'garnet/go/src/fidl/compiler/backend/goldens/union.test.json.cc.golden',
                    ]
                ]
        }
        command = ['test', '--no-regen']
        actual = get_commands(mocks, command)
        expected = set(util.FIDLGEN_TEST_TARGETS) | {util.HLCPP_TEST_TARGET}
        self.assertEqual(len(actual), 2)
        self.assertEqual(actual[0], util.BUILD_FIDLGEN)
        self.assertTestsRun(actual[1], expected)

    def test_fidlgen_dart_changed(self):
        mocks = {
            'get_changed_files': [[
                'topaz/bin/fidlgen_dart/backend/ir/ir.go',
            ]]
        }
        command = ['test', '--no-regen']
        actual = get_commands(mocks, command)
        expected = {util.FIDLGEN_DART_TEST_TARGET}
        self.assertEqual(len(actual), 2)
        self.assertEqual(actual[0], util.BUILD_FIDLGEN_DART)
        self.assertTestsRun(actual[1], expected)

    def test_fidlgen_dart_golden_changed(self):
        mocks = {
            'get_changed_files':
                [
                    [
                        'topaz/bin/fidlgen_dart/goldens/handles.test.json_async.dart.golden',
                    ]
                ]
        }
        command = ['test', '--no-regen']
        actual = get_commands(mocks, command)
        expected = {
            util.DART_TEST_TARGET,
            util.FIDLGEN_DART_TEST_TARGET,
        }
        self.assertEqual(len(actual), 2)
        self.assertEqual(actual[0], util.BUILD_FIDLGEN_DART)
        self.assertTestsRun(actual[1], expected)

    def test_c_runtime_changed(self):
        mocks = {
            'get_changed_files': [[
                'zircon/system/ulib/fidl/txn_header.c',
            ]]
        }
        command = ['test', '--no-regen']
        actual = get_commands(mocks, command)
        expected = {
            util.HLCPP_TEST_TARGET,
            util.LLCPP_TEST_TARGET,
            util.C_TEST_TARGET,
        }
        self.assertEqual(len(actual), 1)
        self.assertTestsRun(actual[0], expected)

    def test_coding_tables_changed(self):
        mocks = {
            'get_changed_files':
                [[
                    'zircon/tools/fidl/goldens/union.test.tables.c.golden',
                ]]
        }
        command = ['test', '--no-regen']
        actual = get_commands(mocks, command)
        expected = {
            util.HLCPP_TEST_TARGET,
            util.LLCPP_TEST_TARGET,
            util.C_TEST_TARGET,
        }
        self.assertEqual(actual[1], util.TEST_FIDLC)
        self.assertTestsRun(actual[2], expected)

    def test_go_runtime_changed(self):
        mocks = {
            'get_changed_files':
                [[
                    'third_party/go/src/syscall/zx/fidl/encoding_new.go',
                ]]
        }
        command = ['test', '--no-regen']
        actual = get_commands(mocks, command)
        expected = {
            util.GO_CONFORMANCE_TEST_TARGET,
            util.GO_TEST_TARGET,
        }
        self.assertEqual(len(actual), 1)
        self.assertTestsRun(actual[0], expected)

    def test_dart_runtime_changed(self):
        mocks = {
            'get_changed_files':
                [
                    [
                        'topaz/public/dart/fidl/lib/src/types.dart',
                        'topaz/public/dart/fidl/lib/src/message.dart',
                    ]
                ]
        }
        command = ['test', '--no-regen']
        actual = get_commands(mocks, command)
        expected = {util.DART_TEST_TARGET}
        self.assertEqual(len(actual), 1)
        self.assertTestsRun(actual[0], expected)

    def test_gidl_changed(self):
        mocks = {
            'get_changed_files':
                [
                    [
                        'tools/fidl/gidl/main.go ',
                        'tools/fidl/gidl/rust/benchmarks.go',
                        'tools/fidl/gidl/rust/conformance.go',
                    ]
                ]
        }
        command = ['test', '--no-regen']
        actual = get_commands(mocks, command)
        expected = {
            util.GIDL_TEST_TARGET,
            util.GO_CONFORMANCE_TEST_TARGET,
            util.HLCPP_CONFORMANCE_TEST_TARGET,
            util.HLCPP_HOST_CONFORMANCE_TEST_TARGET,
            util.LLCPP_CONFORMANCE_TEST_TARGET,
            util.RUST_CONFORMANCE_TEST_TARGET,
            util.DART_TEST_TARGET,
        }
        self.assertEqual(len(actual), 1)
        self.assertTestsRun(actual[0], expected)

    def assertTestsRun(self, raw_command, expected):
        self.assertEqual(raw_command[0], 'fx')
        self.assertEqual(raw_command[1], 'test')
        tests = set(raw_command[2:])
        self.assertSetEqual(tests, expected)


if __name__ == '__main__':
    unittest.main()
