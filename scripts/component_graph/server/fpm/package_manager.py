#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""PackageManager provides an interface to the JSON FPM API.

The PackageManager interface provides a simple way to retrieve data from the
package manager. It combines this data with annotated data from the disk
(which would be packages if not packaged in BootFS due to implementation
details). It does minimal parsing on this data and passes it back to the user.
"""
import json
import os
import re
import urllib.request
from far.far_reader import far_read
from server.util.url import strip_package_version, package_to_url
from server.util.logging import get_logger

def read_package(far_buffer):
    """Performs a raw_read then intelligently restructures known package structures."""
    files = far_read(far_buffer)

    if "meta/contents" in files:
      content = files["meta/contents"].decode()
      files["meta/contents"] = dict(
            [tuple(e.rsplit("=", maxsplit=1)) for e in content.split("\n") if e])
    if "meta/package" in files:
      files["meta/package"] = json.loads(files["meta/package"].decode())
    json_extensions = [".cm", ".cmx"]
    for ext in json_extensions:
        for path in files.keys():
            if path.endswith(ext):
                files[path] = json.loads(files[path])
    return files

class PackageManager:
    """ Interface for communicating with a remote package manager. """

    def __init__(self, url, fuchsia_root):
        self.url = url
        if not self.url.endswith("/"):
            self.url += "/"
        self.package_manager_targets_url = self.url + "targets.json"
        self.package_manager_blobs_url = self.url + "blobs/"
        self.builtin_path = fuchsia_root + \
          "scripts/component_graph/server/static/builtins.json"
        self.logger = get_logger(__name__)

    def ping(self):
        """ Returns true if the ping succeeds else a failure. """
        try:
            with urllib.request.urlopen(self.url):
                return True
        except (urllib.error.URLError, urllib.error.HTTPError):
            return False

    def get_blob(self, merkle):
        """ Returns a blob or none if there is any error. """
        try:
            with urllib.request.urlopen(self.package_manager_blobs_url +
                                        merkle) as blob_response:
                return blob_response.read()
        except (urllib.error.URLError, urllib.error.HTTPError):
            self.logger.warning("Blob: %s does not exist", merkle)
            return None

    def get_builtin_data(self):
        """ Returns the builtin config data as a text string. """
        if os.path.exists(self.builtin_path):
            return open(self.builtin_path, "r").read()
        return ""

    def get_builtin_packages(self):
        """ Returns the builtin packages as a python dict. """
        builtin_data = self.get_builtin_data()
        if builtin_data:
          return json.loads(builtin_data)["packages"]

    def get_matching_package_contents(self, package, data_name_pattern):
      """
      This is a general function that searches the contents of the given package, gets blobs for
      all files in the package whoes name matches the given regex, and then returns the matching
      filenames and contents in a list of tuples.
      """
      if not "meta/contents" in package["files"]:
        return {}
      pattern = re.compile(data_name_pattern)
      data = []
      for name, merkle in package["files"]["meta/contents"].items():
          if not pattern.match(name):
              continue
          blob = self.get_blob(merkle)
          if blob:
              data.append((name, blob))
      return data

    def get_packages(self):
        """ Returns a list of packages available on the system. """
        with urllib.request.urlopen(
                self.package_manager_targets_url) as response:
            targets = json.loads(response.read().decode())
            packages = []
            for pkg_name, pkg_data in targets["signed"]["targets"].items():
              # TODO(benwright) - strip_package_version is likely to change as we may include
                # the variant in a future release.
                package = {
                    "url": package_to_url(strip_package_version(pkg_name)),
                    "merkle": pkg_data["custom"]["merkle"],
                    "type": "package",
                    "files": {},
                }
                blob = self.get_blob(package["merkle"])
                if not blob:
                    continue
                package["files"] = read_package(blob)
                packages.append(package)
            # Append annotations
            for package in self.get_builtin_packages():
                builtin_package = package
                builtin_package["files"] = {}
                builtin_package["merkle"] = "0"
                builtin_package["type"] = "builtin"
                packages.append(builtin_package)
            return packages
        return None
