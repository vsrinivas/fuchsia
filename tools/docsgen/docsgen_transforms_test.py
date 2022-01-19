#!/usr/bin/env fuchsia-vendored-python
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
from unittest import mock

import docsgen_transforms


class DocsgenTransformsTest(unittest.TestCase):

    SAMPLE_TOC = 'toc:\n- title: "Code of conduct"\n  path: /CODE_OF_CONDUCT.md\n- title: "Overview"\n  path: /docs/contribute/testing/README.md'
    SAMPLE_MD = '# Diagnostics Selectors\n[TOC]\n## Listener {#Listener}\nblah blah blah\n### OnGesture {#Listener.OnGesture}\nhahaha'
    UPDATED_REF = '''Project: /_project.yaml
Book: /_book.yaml
edit_url: https://fuchsia.googlesource.com/reference-docs/+show/main/fakefile
bug_url: https://bugs.fuchsia.dev/p/fuchsia/issues/entry?template=Fuchsia.dev%20Documentation&description=Issue%20on%20page:%20https://ci.android.com/edit?repo=fuchsia/fuchsia/main%26file=fakefile
<!-- public markdown source of this page: https://fuchsia.googlesource.com/reference-docs/+show/main/fakefile -->
<!-- The header above is automatically added to this file. Do not modify anything above this line. -->
# Diagnostics Selectors
[TOC]
## Listener {#Listener}
blah blah blah
### OnGesture {#Listener.OnGesture}
hahaha
<!-- The footer below is automatically added to this file. Do not modify anything below this line. -->

'''
    UPDATED_REG = '''Project: /_project.yaml
Book: /_book.yaml
edit_url: https://ci.android.com/edit?repo=fuchsia/fuchsia/main&file=fakefile
bug_url: https://bugs.fuchsia.dev/p/fuchsia/issues/entry?template=Fuchsia.dev%20Documentation&description=Issue%20on%20page:%20https://ci.android.com/edit?repo=fuchsia/fuchsia/main%26file=fakefile
<!-- original source of this page: https://cs.opensource.google/fuchsia/fuchsia/+/main:fakefile -->
<!-- The header above is automatically added to this file. Do not modify anything above this line. -->
# Diagnostics Selectors
[TOC]
## Listener {#Listener}
blah blah blah
### OnGesture {#Listener.OnGesture}
hahaha
<!-- The footer below is automatically added to this file. Do not modify anything below this line. -->
<div class="align-right">
<a href="https://bugs.fuchsia.dev/p/fuchsia/issues/entry?template=Fuchsia.dev%20Documentation&description=Issue%20on%20page:%20https://ci.android.com/edit?repo=fuchsia/fuchsia/main%26file=fakefile" title="Report a bug or suggest improvements on this page"><span class="material-icons" style="font-size: 18px">bug_report</span></a>
<a href="https://cs.opensource.google/fuchsia/fuchsia/+/main:fakefile" title="View the source code of this page"><span class="material-icons" style="font-size: 18px">code</span></a>
<a href="https://ci.android.com/edit?repo=fuchsia/fuchsia/main&file=fakefile" title="Edit the source code of this page"><span class="material-icons" style="font-size: 18px">edit</span></a>
</div>

'''

    def test_add_markdown_footer_and_header_ref(self):
        with mock.patch.object(docsgen_transforms.glob, 'glob') as mock_glob:
            with mock.patch(
                    'builtins.open',
                    mock.mock_open(read_data=self.SAMPLE_MD)) as mock_open:
                mock_glob.return_value = ['fakefile']
                docsgen_transforms._add_markdown_header_and_footer(
                    is_regular_docs=False)
                handle = mock_open()
                handle.write.assert_called_once_with(self.UPDATED_REF)

    def test_add_markdown_footer_and_header_reg(self):
        with mock.patch.object(docsgen_transforms.glob, 'glob') as mock_glob:
            with mock.patch(
                    'builtins.open',
                    mock.mock_open(read_data=self.SAMPLE_MD)) as mock_open:
                mock_glob.return_value = ['fakefile']
                docsgen_transforms._add_markdown_header_and_footer(
                    is_regular_docs=True)
                handle = mock_open()
                handle.write.assert_called_once_with(self.UPDATED_REG)

    def test_remove_toc(self):
        with mock.patch.object(docsgen_transforms.glob, 'glob') as mock_glob:
            with mock.patch(
                    'builtins.open',
                    mock.mock_open(read_data=self.SAMPLE_MD)) as mock_open:
                mock_glob.return_value = ['fakefile']
                docsgen_transforms._remove_toc()
                handle = mock_open()
                handle.write.assert_called_once_with(
                    '# Diagnostics Selectors\n<!-- commented [TOC] -->\n## Listener {#Listener}\nblah blah blah\n### OnGesture {#Listener.OnGesture}\nhahaha'
                )

    def test_rename_readme(self):
        with mock.patch.object(docsgen_transforms.glob, 'glob') as mock_glob:
            with mock.patch.object(docsgen_transforms.os,
                                   'rename') as mock_rename:
                mock_glob.return_value = [
                    'path/to/fake/README.md',
                    'README.md',
                ]
                docsgen_transforms._rename_readme()
                mock_rename.assert_any_call(
                    'path/to/fake/README.md', 'path/to/fake/index.md')
                mock_rename.assert_any_call('README.md', 'index.md')

    def test_canonical_header_anchorid(self):
        with mock.patch.object(docsgen_transforms.glob, 'glob') as mock_glob:
            with mock.patch(
                    'builtins.open',
                    mock.mock_open(read_data=self.SAMPLE_MD)) as mock_open:
                mock_glob.return_value = ['fakefile']
                docsgen_transforms._canonical_header_anchorid()
                handle = mock_open()
                handle.write.assert_called_once_with(
                    '# Diagnostics Selectors\n[TOC]\n## Listener {:#Listener transformation="converted"}\nblah blah blah\n### OnGesture {:#Listener.OnGesture transformation="converted"}\nhahaha'
                )

    def test_transform_toc_files_regular(self):
        with mock.patch.object(docsgen_transforms.glob, 'glob') as mock_glob:
            with mock.patch(
                    'builtins.open',
                    mock.mock_open(read_data=self.SAMPLE_TOC)) as mock_open:

                mock_glob.return_value = ['fakefile']
                docsgen_transforms._transform_toc_files(is_regular_docs=True)
                handle = mock_open()
                handle.writelines.assert_called_once_with(
                    [
                        'toc:\n', '- title: "Code of conduct"\n',
                        '  path: /fuchsia-src/CODE_OF_CONDUCT.md\n',
                        '- title: "Overview"\n',
                        '  path: /fuchsia-src/contribute/testing/index.md'
                    ])

    def test_transform_toc_files_reference(self):
        with mock.patch.object(docsgen_transforms.glob, 'glob') as mock_glob:
            with mock.patch(
                    'builtins.open',
                    mock.mock_open(read_data=self.SAMPLE_TOC)) as mock_open:

                mock_glob.return_value = ['fakefile']
                docsgen_transforms._transform_toc_files(is_regular_docs=False)
                handle = mock_open()
                handle.writelines.assert_called_once_with(
                    [
                        'toc:\n', '- title: "Code of conduct"\n',
                        '  path: /CODE_OF_CONDUCT.md\n',
                        '- title: "Overview"\n',
                        '  path: /docs/contribute/testing/index.md'
                    ])
