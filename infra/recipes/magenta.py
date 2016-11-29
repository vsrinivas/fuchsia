# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Recipe for building Magenta."""

import multiprocessing

from recipe_engine.recipe_api import Property


DEPS = [
    'infra/jiri',
    'recipe_engine/path',
    'recipe_engine/properties',
    'recipe_engine/raw_io',
    'recipe_engine/step',
]

TARGETS = [ 'magenta-qemu-arm32', 'magenta-qemu-arm64', 'magenta-pc-x86-64' ]

PROPERTIES = {
    'category': Property(kind=str, help='Build category', default=None),
    'patch_gerrit_url': Property(kind=str, help='Gerrit host', default=None),
    'patch_project': Property(kind=str, help='Gerrit project', default=None),
    'patch_ref': Property(kind=str, help='Gerrit patch ref', default=None),
    'patch_storage': Property(kind=str, help='Patch location', default=None),
    'patch_repository_url': Property(kind=str, help='URL to a Git repository',
                              default=None),
    'manifest': Property(kind=str, help='Jiri manifest to use'),
    'remote': Property(kind=str, help='Remote manifest repository'),
    'target': Property(kind=str, help='Target to build'),
    'toolchain': Property(kind=str, help='Toolchain to use'),
}


def RunSteps(api, category, patch_gerrit_url, patch_project, patch_ref,
             patch_storage, patch_repository_url, manifest, remote, target,
             toolchain):
    assert target in TARGETS, 'unsupported target'
    assert toolchain in ['gcc', 'clang'], 'unsupported toolchain'

    api.jiri.ensure_jiri()

    api.jiri.set_config('magenta')

    api.jiri.init()
    api.jiri.clean_project()
    api.jiri.import_manifest(manifest, remote, overwrite=True)
    api.jiri.update(gc=True)
    step_result = api.jiri.snapshot(api.raw_io.output())
    snapshot = step_result.raw_io.output
    step_result.presentation.logs['jiri.snapshot'] = snapshot.splitlines()

    if patch_ref is not None:
        api.jiri.patch(patch_ref, host=patch_gerrit_url, delete=True, force=True)

    assert 'checkout' not in api.path
    api.path['checkout'] = api.path['slave_build'].join('magenta')

    with api.step.nest('build'):
        api.step('cleanup', ['make', 'spotless'], cwd=api.path['checkout'])
        build_args = ['make', '-j%s' % multiprocessing.cpu_count(), target]
        if toolchain == 'clang':
            build_args.append('USE_CLANG=true')
        api.step('build', build_args, cwd=api.path['checkout'])


def GenTests(api):
    yield api.test('ci') + api.properties(
        manifest='magenta',
        remote='https://fuchsia.googlesource.com/manifest',
        target='magenta-pc-x86-64',
        toolchain='clang',
    )
    yield api.test('cq_try') + api.properties.tryserver(
        gerrit_project='magenta',
        patch_gerrit_url='fuchsia-review.googlesource.com',
        manifest='magenta',
        remote='https://fuchsia.googlesource.com/manifest',
        target='magenta-pc-x86-64',
        toolchain='clang',
    )
