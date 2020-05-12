import os
import subprocess

from env import FUCHSIA_DIR, BUILD_DIR, MODE, PLATFORM

ZN_DIR = BUILD_DIR + '.zircon'
TOPAZ_DIR = os.path.join(FUCHSIA_DIR, 'topaz')
GO_DIR = os.path.join(FUCHSIA_DIR, 'third_party/go')
HOST_DIR = 'host-x64-{}-{}'.format(PLATFORM, MODE)

FIDLC_DIR = 'zircon/tools/fidl'
FIDLGEN_DIR = 'garnet/go/src/fidl/compiler'
FIDLGEN_DART_DIR = 'topaz/bin/fidlgen_dart'
FIDLGEN_GO_DIR = 'tools/fidl/fidlgen_go'
FIDLGEN_BACKEND_DIRS = [
    'garnet/go/src/fidl/compiler/llcpp_backend',
    FIDLGEN_GO_DIR,
    'tools/fidl/fidlgen_hlcpp',
    'tools/fidl/fidlgen_libfuzzer',
    'tools/fidl/fidlgen_rust',
    'tools/fidl/fidlgen_syzkaller',
]

TEST_FIDLC = os.path.join(
    ZN_DIR, HOST_DIR, 'obj/system/utest/fidl-compiler/fidl-compiler-test.debug')
FIDLGEN_TEST_TARGETS = [
    '//garnet/go/src/fidl',
    '//tools/fidl/fidlgen_hlcpp',
    '//tools/fidl/fidlgen_go',
    '//tools/fidl/fidlgen_libfuzzer',
    '//tools/fidl/fidlgen_rust',
    '//tools/fidl/fidlgen_syzkaller',
]
FIDLGEN_DART_TEST_TARGET = '//topaz/bin/fidlgen_dart'
HLCPP_TEST_TARGET = '//sdk/lib/fidl'
LLCPP_TEST_TARGET = '//src/lib/fidl/llcpp'
C_TEST_TARGET = '//src/lib/fidl/c'
GO_TEST_TARGET = 'go_fidl_test'
RUST_TEST_TARGET = '//src/lib/fidl/rust'
DART_TEST_TARGET = 'fidl_bindings_test'
GIDL_TEST_TARGET = '//tools/fidl/gidl'

HLCPP_CONFORMANCE_TEST_TARGET = 'conformance_test'
HLCPP_HOST_CONFORMANCE_TEST_TARGET = 'fidl_cpp_host_conformance_test'
LLCPP_CONFORMANCE_TEST_TARGET = 'fidl_llcpp_conformance_test'
GO_CONFORMANCE_TEST_TARGET = 'fidl_go_conformance'
RUST_CONFORMANCE_TEST_TARGET = 'fidl_conformance_tests'

HLCPP_RUNTIME = 'sdk/lib/fidl'
LLCPP_RUNTIME = 'src/lib/fidl/llcpp'
C_RUNTIME = 'zircon/system/ulib/fidl'
GO_RUNTIME = 'third_party/go/src/syscall/zx/fidl'
RUST_RUNTIME = 'src/lib/fidl/rust'
DART_RUNTIME = 'topaz/public/dart/fidl/lib'

BUILD_FIDLC = [
    'fx',
    'ninja',
    '-C',
    ZN_DIR,
    os.path.join(HOST_DIR, 'obj/tools/fidl/fidlc'.format(HOST_DIR)),
    os.path.join(HOST_DIR, 'obj/system/utest/fidl-compiler/fidl-compiler-test'),
]
BUILD_FIDLGEN = ['fx', 'build', 'garnet/go/src/fidl']
BUILD_FIDLGEN_DART = ['fx', 'ninja', '-C', BUILD_DIR, 'host_x64/fidlgen_dart']


def run(command, dry_run):
    if dry_run:
        print('would run: {}'.format(command))
    else:
        subprocess.call(command)


def get_changed_files():
    """
    Return a List of paths relative to FUCHSIA_DIR of changed files relative to
    the parent. This uses the same logic as fx format-code.
    """
    upstream = "origin/master"
    local_commit = subprocess.check_output(
        "git rev-list HEAD ^{} -- 2>/dev/null | tail -1".format(upstream),
        shell=True).strip().decode()
    diff_base = subprocess.check_output(
        ['git', 'rev-parse', local_commit +
         '^']).strip().decode() if local_commit else "HEAD"
    files = subprocess.check_output(['git', 'diff', '--name-only',
                                     diff_base]).strip().decode().split('\n')

    repo = subprocess.check_output(['git', 'rev-parse',
                                    '--show-toplevel']).strip().decode()
    # add prefixes so that all and targets can be specified relative to FUCHSIA_DIR
    if repo.endswith('topaz'):
        files = [os.path.join('topaz', p) for p in files]
    elif repo.endswith('third_party/go'):
        files = [os.path.join('third_party/go', p) for p in files]

    return files
