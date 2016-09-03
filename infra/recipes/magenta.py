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
    'recipe_engine/step',
]

TARGETS = [ 'magenta-qemu-arm32', 'magenta-qemu-arm64', 'magenta-pc-x86-64' ]

PROPERTIES = {
    'gerrit': Property(kind=str, help='Gerrit host', default=None,
                     param_name='gerrit_host'),
    'patch_project': Property(kind=str, help='Gerrit project', default=None,
                              param_name='gerrit_project'),
    'event.patchSet.ref': Property(kind=str, help='Gerrit patch ref',
                                   default=None, param_name='gerrit_patch_ref'),
    'repository': Property(kind=str, help='Full url to a Git repository',
                           default=None, param_name='repo_url'),
    'refspec': Property(kind=str, help='Refspec to checkout', default='master'),
    'category': Property(kind=str, help='Build category', default=None),
    'manifest': Property(kind=str, help='Jiri manifest to use'),
    'remote': Property(kind=str, help='Remote manifest repository'),
    'target': Property(kind=str, help='Target to build'),
}


def RunSteps(api, category, repo_url, refspec, gerrit_host, gerrit_project,
             gerrit_patch_ref, manifest, remote, target):
    if category == 'cq':
        assert gerrit_host.startswith('https://')
        repo_url = '%s/%s' % (gerrit_host.rstrip('/'), gerrit_project)
        refspec = gerrit_patch_ref

    assert repo_url and refspec, 'repository url and refspec must be given'
    assert repo_url.startswith('https://')

    api.jiri.ensure_jiri()
    api.jiri.set_config('magenta')

    api.jiri.init()
    api.jiri.clean_project()
    api.jiri.import_manifest(manifest, remote, overwrite=True)
    api.jiri.update(gc=True)
    if category == 'cq':
        api.jiri.patch(gerrit_patch_ref, host=gerrit_host, delete=True, force=True)

    assert 'checkout' not in api.path
    api.path['checkout'] = api.path['slave_build'].join('magenta')

    with api.step.nest('build'):
        api.step('cleanup', ['make', 'spotless'], cwd=api.path['checkout'])
        api.step('build',
                ['make', '-j%s' % multiprocessing.cpu_count(), target],
                cwd=api.path['checkout'])


def GenTests(api):
    yield api.test('scheduler') + api.properties(
        repository='https://fuchsia.googlesource.com/magenta',
        refspec='master',
        manifest='magenta',
        remote='https://fuchsia.googlesource.com/manifest',
        target='magenta-pc-x86-64',
    )
    yield api.test('cq') + api.properties.tryserver_gerrit(
        'magenta',
        gerrit_host='https://fuchsia-review.googlesource.com',
        manifest='magenta',
        remote='https://fuchsia.googlesource.com/manifest',
        target='magenta-pc-x86-64',
    )
