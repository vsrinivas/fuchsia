#!/usr/bin/env python
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""A script used to update the git url and revisions for SwiftShader prebuilts.

Tries to intelligently determine the revisions of some of the dependencies by
parsing glslang's known_good.json file, and SPIRV-Tools' DEPS file.

The output is a shell script fragment that can be sourced to define various
git url variables used by the rebuild script.
"""

from __future__ import print_function

import argparse
import base64
import configparser
import json
import os
import string
import subprocess
import sys
import urllib2


def get_git_file_data(url, revision, file_path):
    """Download a single file from a git source repository."""
    if url.startswith('https://github.com/'):
        # Use github-specific URL API:
        data = urllib2.urlopen('%s/raw/%s/%s' % (url, revision, file_path))
        return data.read()
    if url.find('.googlesource.com') >= 0:
        url = '%s/+/%s/%s?format=TEXT' % (url, revision, file_path)
        data_file = urllib2.urlopen(url)
        data = data_file.read()
        data = base64.b64decode(data)
        return data

    raise Exception('Unsupported URL type: ' + url)


def get_git_remote_ref(git_url, ref_name):
    """Get the hash of a remote git url reference."""
    ref_name = 'refs/' + ref_name
    command = ['git', 'ls-remote', '--refs', git_url, ref_name]
    output = subprocess.check_output(command)
    for line in output.splitlines():
        commit, _, ref = line.partition('\t')
        if ref == ref_name:
            return commit
    return None


def parse_known_good_file(good_data):
    """Parse a known_good.json file and extract its git url + revisions from it.

    Args:
        good_data: Content of known_good.json file as string.
    Returns:
        A dictionary mapping each dependency name to its git url@revision.
        Note that dependency names could contain directories (e.g.
        'spirv-tools/external/spirv-headers')
    Note:
        See _EXAMPLE_KNOWN_GOOD_JSON_FILE below to see what the file should
        look like.
    """
    result = {}
    SITE_MAP = {'github': 'https://github.com'}
    deps = json.loads(good_data)
    assert 'commits' in deps
    for dep in deps['commits']:
        name = dep['name']
        site = dep['site']
        site_url = SITE_MAP.get(site)
        assert site_url, 'Unknown site value: %s' % site
        subrepo = dep['subrepo']
        revision = dep['commit']
        result[str(name)] = '{0}/{1}@{2}'.format(site_url, subrepo, revision)
    return result


def parse_deps_file(deps_data):
    """Parse a DEPS file to extract git url + revisions from it.

    Args:
        deps_data: Content of DEPS file as a string.
    Returns:
        Dictionary mapping each dependency name to its git url@revision.
    Note:
        See _EXAMPLE_DEPS_FILE below to see what the file should look like.
    """
    assert isinstance(deps_data, basestring)
    var_func = lambda name: safe
    safe_globals = {
        '__builtins__': {
            'True': True,
            'False': False,
        },
        # The Var() function is used to peek into the 'vars' dictionary inside
        # the DEPS file, this can be implemented with a lambda.
        'Var': lambda name: safe_globals['vars'][name]
    }

    exec deps_data in safe_globals
    deps = safe_globals.get('deps')
    assert isinstance(deps, dict)
    return deps


def parse_git_submodules(gitmodules_data):
    """Parse a .gitmodules file to extract a { name -> url } map from it."""
    result = {}
    # NOTE: configparser.ConfigParser() doesn't seem to like the file
    #       (i.e. read_string() always returns None), so do the parsing
    #       manually here.
    section_name = None
    in_submodule_section = False
    submodule_name = None
    submodule_prefix = 'submodule "'
    urls = {}
    branches = {}
    for line in gitmodules_data.splitlines():
        if line.startswith('['):
            section_name = line[1:-1]
            is_submodule_section = section_name.startswith(submodule_prefix)
            if is_submodule_section:
                submodule_name = section_name[len(submodule_prefix):-1]
        elif is_submodule_section:
            key, _, value = line.strip().partition('=')
            if not value:
                continue
            key = key.strip()
            value = value.strip()
            if key == 'url':
                urls[submodule_name] = value
            elif key == 'branch':
                branches[submodule_name] = value

    result = {}
    for submodule, url in urls.iteritems():
        branch = branches.get(submodule)
        if not branch:
            branch = get_git_remote_ref(url, 'heads/master')
        result[submodule] = '%s@%s' % (url, branch)
    return result


_SWIFTSHADER_URL = 'https://swiftshader.googlesource.com/SwiftShader'
_GLSLANG_URL = 'https://github.com/KhronosGroup/glslang'
_VULKAN_HEADERS_URL = 'https://github.com/KhronosGroup/Vulkan-Headers'
_VULKAN_LOADER_URL = 'https://github.com/KhronosGroup/Vulkan-Loader'
_VULKAN_VALIDATION_LAYERS_URL = 'https://github.com/KhronosGroup/Vulkan-ValidationLayers'

_DEFAULT_SWIFTSHADER_REVISION = 'd0b7d1e354dfece95df97c4344ab55cd2cecdedf'
_DEFAULT_GLSLANG_REVISION = '3ee5f2f1d3316e228916788b300d786bb574d337'
_DEFAULT_VULKAN_SDK_VERSION = '1.2.148'


def make_git_url(site_url, revision):
    return '{site_url}@{revision}'.format(site_url=site_url, revision=revision)


def main(args):
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        '--swiftshader-revision',
        default=_DEFAULT_SWIFTSHADER_REVISION,
        metavar='REVISION',
        help='SwiftShader revision [%s].' % _DEFAULT_SWIFTSHADER_REVISION[:16])

    parser.add_argument(
        '--glslang-revision',
        default=_DEFAULT_GLSLANG_REVISION,
        metavar='REVISION',
        help='glslang revision [%s].' % _DEFAULT_GLSLANG_REVISION[:16])

    parser.add_argument(
        '--vulkan-sdk-version',
        default=_DEFAULT_VULKAN_SDK_VERSION,
        metavar='VERSION',
        help='Vulkan SDK version [%s].' % _DEFAULT_VULKAN_SDK_VERSION)

    args = parser.parse_args(args[1:])

    vulkan_headers_revision = 'sdk-%s.0' % args.vulkan_sdk_version
    vulkan_loader_revision = vulkan_headers_revision
    vulkan_validation_layers_revision = vulkan_headers_revision

    d = {
        'script_name':
            os.path.basename(__file__),
        'swiftshader':
            make_git_url(_SWIFTSHADER_URL, args.swiftshader_revision),
        'glslang':
            make_git_url(_GLSLANG_URL, args.glslang_revision),
        'vulkan_headers':
            make_git_url(_VULKAN_HEADERS_URL, vulkan_headers_revision),
        'vulkan_loader':
            make_git_url(_VULKAN_LOADER_URL, vulkan_loader_revision),
        'vulkan_validation_layers':
            make_git_url(
                _VULKAN_VALIDATION_LAYERS_URL,
                vulkan_validation_layers_revision),
    }

    # Add SwiftShader submodule dependencies.
    swiftshader_gitmodules = get_git_file_data(
        _SWIFTSHADER_URL, args.swiftshader_revision, '.gitmodules')
    swiftshader_submodules = parse_git_submodules(swiftshader_gitmodules)
    d['cppdap'] = swiftshader_submodules['third_party/cppdap']
    d['json'] = swiftshader_submodules['third_party/json']
    d['libbacktrace'] = swiftshader_submodules['third_party/libbacktrace/src']

    # Add glslang dependencies.
    known_good = get_git_file_data(
        _GLSLANG_URL, args.glslang_revision, 'known_good.json')
    glslang_deps = parse_known_good_file(known_good)
    d['spirv_tools'] = glslang_deps['spirv-tools']
    d['spirv_headers'] = glslang_deps['spirv-tools/external/spirv-headers']

    # Add SPIRV-Tools dependencies.
    spirv_tools_url, _, spirv_tools_revision = d['spirv_tools'].partition('@')
    spirv_tools_deps_file = get_git_file_data(
        spirv_tools_url, spirv_tools_revision, 'DEPS')
    spirv_tools_deps = parse_deps_file(spirv_tools_deps_file)
    d['effcee'] = spirv_tools_deps['external/effcee']
    d['re2'] = spirv_tools_deps['external/re2']

    output = string.Template(
        r'''# Auto-generated by ${script_name} - DO NOT EDIT!
# To be used with $$FUCHSIA_SOURCE/scripts/prebuilts/swiftshader/build-linux-host-prebuilts.sh

SWIFTSHADER_GIT_URL=${swiftshader}

GLSLANG_GIT_URL=${glslang}

VULKAN_HEADERS_GIT_URL=${vulkan_headers}
VULKAN_LOADER_GIT_URL=${vulkan_loader}
VULKAN_VALIDATION_LAYERS_GIT_URL=${vulkan_validation_layers}

# NOTE: SPIRV-Tools and SPIRV-Headers revisions from glslang/known_good.json
SPIRV_HEADERS_GIT_URL=${spirv_headers}
SPIRV_TOOLS_GIT_URL=${spirv_tools}

# NOTE: The following are Swiftshader deps (for --enable-debugger-support only)
CPPDAP_GIT_URL=${cppdap}
JSON_GIT_URL=${json}
LIBBACKTRACE_GIT_URL=${libbacktrace}

# NOTE: effcee and re2 revisions from SPIRV-Tools/DEPS
EFFCEE_GIT_URL=${effcee}
RE2_GIT_URL=${re2}
''').substitute(d)

    print(output)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

# Example files for the parser above. No real purpose except documenting what
# the expected format is.

_EXAMPLE_KNOWN_GOOD_JSON_FILE = r'''
{
  "commits" : [
    {
      "name" : "spirv-tools",
      "site" : "github",
      "subrepo" : "KhronosGroup/SPIRV-Tools",
      "subdir" : "External/spirv-tools",
      "commit" : "5c019b5923c1f6bf00a3ac28114ec4a7b1faa0e2"
    },
    {
      "name" : "spirv-tools/external/spirv-headers",
      "site" : "github",
      "subrepo" : "KhronosGroup/SPIRV-Headers",
      "subdir" : "External/spirv-tools/external/spirv-headers",
      "commit" : "204cd131c42b90d129073719f2766293ce35c081"
    }
  ]
}
'''

_EXAMPLE_DEPS_FILE = r'''
use_relative_paths = True

vars = {
  'github': 'https://github.com',

  'effcee_revision': 'cd25ec17e9382f99a895b9ef53ff3c277464d07d',
  'googletest_revision': 'f2fb48c3b3d79a75a88a99fba6576b25d42ec528',
  're2_revision': '5bd613749fd530b576b890283bfb6bc6ea6246cb',
  'spirv_headers_revision': 'af64a9e826bf5bb5fcd2434dd71be1e41e922563',
}

deps = {
  'external/effcee':
      Var('github') + '/google/effcee.git@' + Var('effcee_revision'),

  'external/googletest':
      Var('github') + '/google/googletest.git@' + Var('googletest_revision'),

  'external/re2':
      Var('github') + '/google/re2.git@' + Var('re2_revision'),

  'external/spirv-headers':
      Var('github') +  '/KhronosGroup/SPIRV-Headers.git@' +
          Var('spirv_headers_revision'),
}
'''

_EXAMPLE_GITMODULES_FILE = r'''
[submodule "third_party/cppdap"]
        path = third_party/cppdap
        url = https://github.com/google/cppdap

[submodule "third_party/googletest"]
        path = third_party/googletest
        url = https://github.com/google/googletest.git

[submodule "third_party/json"]
        path = third_party/json
        url = https://github.com/nlohmann/json.git

[submodule "third_party/libbacktrace/src"]
        path = third_party/libbacktrace/src
        url = https://github.com/ianlancetaylor/libbacktrace.git

[submodule "third_party/PowerVR_Examples"]
        path = third_party/PowerVR_Examples
        url = https://github.com/powervr-graphics/Native_SDK.git
'''
