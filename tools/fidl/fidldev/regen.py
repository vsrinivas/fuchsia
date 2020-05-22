import os

import util

FIDLC_REGEN = 'zircon/tools/fidl/testdata/regen.sh'
FIDLGEN_REGEN = 'garnet/go/src/fidl/compiler/backend/typestest/regen.sh'
FIDLGEN_DART_REGEN = 'topaz/bin/fidlgen_dart/regen.sh'
GO_BINDINGS_REGEN = 'third_party/go/regen-fidl'


def regen_fidlc_goldens(build_first, dry_run):
    if build_first:
        util.run(util.BUILD_FIDLC, dry_run, exit_on_failure=True)
    util.run(path_to_regen_command(FIDLC_REGEN), dry_run, exit_on_failure=True)


def regen_fidlgen_goldens(build_first, dry_run):
    if build_first:
        util.run(util.BUILD_FIDLGEN, dry_run, exit_on_failure=True)
    util.run(
        path_to_regen_command(FIDLGEN_REGEN), dry_run, exit_on_failure=True)


def regen_fidlgendart_goldens(build_first, dry_run):
    if build_first:
        util.run(util.BUILD_FIDLGEN_DART, dry_run, exit_on_failure=True)
    util.run(
        path_to_regen_command(FIDLGEN_DART_REGEN),
        dry_run,
        exit_on_failure=True)


def regen_go_bindings(build_first, dry_run, exit_on_failure=True):
    util.run(
        path_to_regen_command(GO_BINDINGS_REGEN), dry_run, exit_on_failure=True)


def path_to_regen_command(path):
    return ['fx', 'exec', os.path.join(util.FUCHSIA_DIR, path)]


def is_ir_changed():
    for path in util.get_changed_files():
        if path.startswith(
                util.FIDLC_DIR) and path.endswith('.test.json.golden'):
            return True
    return False


def is_go_bindings_changed():
    for path in util.get_changed_files():
        if path.startswith(
                util.FIDLGEN_DIR) and path.endswith('.test.json.go.golden'):
            return True
    return False


REGEN_TARGETS = [
    ('fidlc', regen_fidlc_goldens),
    ('fidlgen', regen_fidlgen_goldens),
    ('fidlgen_dart', regen_fidlgendart_goldens),
    ('go', regen_go_bindings),
]


def regen_explicit(targets, build_first, dry_run):
    for target, func in REGEN_TARGETS:
        if target in targets or 'all' in targets:
            func(build_first, dry_run)


def regen_changed(changed_files, build_first, dry_run):
    regen_fidlc = False
    regen_fidlgen = False
    regen_fidlgendart = False
    regen_go = False
    for file_ in changed_files:
        if file_.startswith(util.FIDLC_DIR):
            regen_fidlc = True
        if file_.startswith(util.FIDLGEN_DIR) or is_in_fidlgen_backend(file_):
            regen_fidlgen = True
        if file_.startswith(util.FIDLGEN_DART_DIR):
            regen_fidlgendart = True
        if file_.startswith(util.FIDLGEN_GO_DIR):
            regen_go = True

    if regen_fidlc:
        regen_fidlc_goldens(build_first, dry_run)
        if dry_run or is_ir_changed():
            regen_fidlgen = True
            regen_fidlgendart = True

    if regen_fidlgen:
        regen_fidlgen_goldens(build_first, dry_run)
        if dry_run or is_go_bindings_changed():
            regen_go = True

    if regen_fidlgendart:
        regen_fidlgendart_goldens(build_first, dry_run)

    if regen_go:
        regen_go_bindings(build_first, dry_run)


def is_in_fidlgen_backend(path):
    return any(path.startswith(b) for b in util.FIDLGEN_BACKEND_DIRS)
