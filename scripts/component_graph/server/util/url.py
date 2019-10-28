#!/usr/bin/env python3
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Provides fuchsia url conversion utility functions."""


def package_to_url(package_name):
    """Takes a component name and returns a fuchsia pkg url"""
    return "fuchsia-pkg://fuchsia.com/{name}".format(name=package_name)


def package_resource_url(package_url, resource_path):
    """ Converts a package and resource path into a fuchsia pkg url with meta. """
    return package_url + "#" + resource_path


def strip_package_version(package_name):
    """Takes a component name with a version and strips the version"""
    if not "/" in package_name:
        return package_name
    return package_name.split("/")[0]
