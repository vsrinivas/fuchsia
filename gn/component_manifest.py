# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import urlparse
import re

__all__ = ['ComponentManifest', 'url_to_path']

FUCHSIA_COMPONENT = 'fuchsia:component'
FUCHSIA_PROGRAM = 'fuchsia:program'
FUCHSIA_RESOURCES = 'fuchsia:resources'

class ComponentFile(object):
    def __init__(self, src_path, url):
        self.src_path = src_path
        self.url = url
        self.url_as_path = url_to_path(url)

class ComponentManifest(object):
    def __init__(self, path):
        self.path = path
        self.manifest_dir = os.path.dirname(path)
        self.json = json.load(open(path))
        if FUCHSIA_COMPONENT not in self.json:
            raise Exception('%s missing %s facet' % (path, FUCHSIA_COMPONENT))
        if 'url' not in self.json[FUCHSIA_COMPONENT]:
            raise Exception('%s missing %s/url' % (path, FUCHSIA_COMPONENT))
        self.url = self.json['fuchsia:component']['url']
        # TODO(ianloic): validate that the manifest makes sense

    def resource_url(self, name):
        if name is None: return None
        return self.json[FUCHSIA_RESOURCES][name]

    def resources(self):
        return self.json[FUCHSIA_RESOURCES]

    def program_resource(self):
        return self.json.get(FUCHSIA_PROGRAM, {}).get('resource')

    def program_url(self):
        return self.resource_url(self.program_resource())

    def files(self):
        files = {'MANIFEST': ComponentFile(src_path=self.path, url=self.url)}
        for name, relative in self.resources().items():
            files[name] = ComponentFile(src_path=os.path.join(self.manifest_dir, relative),
                                        url=urlparse.urljoin(self.url, relative))
        return files


def url_to_path(url):
    return re.sub(r'[:/]+', '/', url)
