import functools
import pprint
import util
import regen


def startswith(prefix):
    return lambda s: s.startswith(prefix)


def endswith(suffix):
    return lambda s: s.endswith(suffix)


# List of test groups. Each test group is of the following structure:
#   (NAME, (PREDICATES, TARGETS, BUILD COMMAND))
# where:
#   - NAME is a name for the group of tests. This name is used to explicitly
#     invoke this test group on the command line (e.g. fidldev test foo would call
#     fx test on the TARGETS for group foo)
#   - PREDICATES is a list of predicates P such that P(file) returns true if the
#     test group should run when |file| is changed
#   - TARGETS is a list of test names as supported by `fx test`, e.g.
#     fully-formed Fuchsia Package URLs, package names, or directories
#   - BUILD COMMAND is any command that should be run prior to running the test
#     group. It can be None if no build step is required, and is skipped if the
#     --no-build flag is passed in. It currently needs to be a List - run_tests
#     needs to be updated to support strings
TEST_GROUPS = [
    (
        'fidlc', (
            [startswith('zircon/tools/fidl')], [util.TEST_FIDLC],
            util.BUILD_FIDLC_TESTS)),

    # it's possible to be more selective on which changes map to which tests,
    # but since fidlgen tests are fast to build and run, just do a blanket
    # check.
    (
        'fidlgen', (
            [startswith(util.FIDLGEN_DIR)] +
            [startswith(p) for p in util.FIDLGEN_BACKEND_DIRS],
            util.FIDLGEN_TEST_TARGETS, util.BUILD_FIDLGEN)),
    (
        'fidlgen_dart', (
            [startswith(util.FIDLGEN_DART_DIR)],
            [util.FIDLGEN_DART_TEST_TARGET], util.BUILD_FIDLGEN_DART)),
    (
        'hlcpp', (
            [
                endswith('test.json.cc.golden'),
                endswith('test.json.h.golden'),
                endswith('test.tables.c.golden'),
                startswith(util.HLCPP_RUNTIME),
                startswith(util.C_RUNTIME),
            ], [util.HLCPP_TEST_TARGET], None)),
    (
        'llcpp', (
            [
                endswith('test.json.llcpp.cc.golden'),
                endswith('test.json.llcpp.h.golden'),
                endswith('test.tables.c.golden'),
                startswith(util.LLCPP_RUNTIME),
                startswith(util.C_RUNTIME)
            ], [util.LLCPP_TEST_TARGET], None)),
    (
        'c',
        (
            # normally, changes to the generated bindings are detected by looking at the
            # goldens. Since we can't do this for C, we look at the coding table goldens
            # and the c_generator instead.
            [
                endswith('test.tables.c.golden'),
                startswith('zircon/tools/fidl/include/fidl/c_generator.h'),
                startswith('zircon/tools/fidl/lib/c_generator.cc'),
                startswith(util.C_RUNTIME),
            ],
            # NOTE: fidl-test should also run, but this script only supports component
            # tests
            [util.C_TEST_TARGET],
            None)),
    (
        'go', (
            [endswith('test.json.go.golden'),
             startswith(util.GO_RUNTIME)],
            [util.GO_TEST_TARGET, util.GO_CONFORMANCE_TEST_TARGET], None)),
    (
        'rust', (
            [endswith('test.json.rs.golden'),
             startswith(util.RUST_RUNTIME)], [util.RUST_TEST_TARGET], None)),
    (
        'dart', (
            [
                endswith('test.json_async.dart.golden'),
                endswith('test.json_test.dart.golden'),
                startswith(util.DART_RUNTIME)
            ], [util.DART_TEST_TARGET], None)),
    (
        'gidl',
        (
            [startswith('tools/fidl/gidl')],
            [
                util.GIDL_TEST_TARGET,
                util.GO_CONFORMANCE_TEST_TARGET,
                util.HLCPP_CONFORMANCE_TEST_TARGET,
                util.HLCPP_HOST_CONFORMANCE_TEST_TARGET,
                util.LLCPP_CONFORMANCE_TEST_TARGET,
                util.RUST_CONFORMANCE_TEST_TARGET,
                # dart conformance is bundled into the rest of the tests
                util.DART_TEST_TARGET
            ],
            None)),
]


def test_explicit(targets, build_first, dry_run, interactive, fx_test_args):
    """ Test an explicit set of test groups """
    tests = []
    for name, test in TEST_GROUPS:
        if name in targets or 'all' in targets:
            tests.append(test)
    return run_tests(tests, build_first, dry_run, interactive, fx_test_args)


def test_changed(
        changed_files, build_first, dry_run, interactive, fx_test_args):
    """ Test relevant test groups given a set of changed files """
    tests = []
    for _, test in TEST_GROUPS:
        (predicates, _, _) = test
        for file_ in changed_files:
            if any(p(file_) for p in predicates):
                tests.append(test)
    return run_tests(tests, build_first, dry_run, interactive, fx_test_args)


def run_tests(tests, build_first, dry_run, interactive, fx_test_args):
    already_built = set()
    test_targets = set()
    manual_tests = set()
    for name, targets, build in tests:
        if build_first and build is not None and tuple(
                build) not in already_built:
            already_built.add(tuple(build))
            util.run(build, dry_run, exit_on_failure=True)

        for target in targets:
            if is_manual_test(target):
                manual_tests.add(target)
            else:
                test_targets.add(target)

    manual_tests = list(manual_tests)
    test_targets = list(test_targets)
    if interactive:
        print('all tests: ')
        pprint.pprint(manual_tests + test_targets)
        manual_tests = interactive_filter(manual_tests)
        test_targets = interactive_filter(test_targets)

    success = True
    for cmd in manual_tests:
        success = success and util.run(cmd, dry_run)
        # print test line that can be copied into a commit message
        # the absolute FUCHSIA_DIR paths are stripped for readability and
        # because they are user specific
        print('Test: ' + cmd.replace(str(util.FUCHSIA_DIR) + '/', ''))

    if test_targets:
        cmd = ['fx', 'test'] + fx_test_args.split()
        if not build_first:
            cmd.append('--no-build')
        # group all tests into a single `fx test` invocation so that the summary
        # prints all results
        cmd.extend(test_targets)
        success = success and util.run(cmd, dry_run)
        print('Test: ' + ' '.join(cmd))

    return success


def interactive_filter(test_targets):
    if not test_targets:
        return []
    filtered = []
    for test in test_targets:
        if input('run {}? (Y/n) '.format(test)) == 'n':
            continue
        filtered.append(test)
    return filtered


def is_manual_test(test):
    """
    Return whether this is meant to be called with fx test or used as a
    standalone test command.
    """
    # currently fidlc is the only test that doesn't use fx test, since it
    # uses some fidlc/fidl-compiler-test binary that is not built with the
    # usual build commands (like fx build zircon/tools, fx ninja -C out/default
    # host_x64/fidlc)
    return test == util.TEST_FIDLC
