# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Scrutiny Example: Zircon Boot Image Extractor
#
# This Scrutiny script demonstrates how the auditing framework can leverage its
# build model to locate the update package and seamlessly extract it from just
# a provided URL automatically resolving the BlobFS merkles and dumping it to
# an output folder. Following this the script shows how you can dig deeper into
# the package by extracting the Zircon Boot Image from the update package where
# you can explore the different sections of the image and even see the
# files located in BootFS.

# 1. Extract the update package.
print "Extracting the update package to /tmp/demo/update"
tool.package.extract --url fuchsia-pkg://fuchsia.com/update --output /tmp/demo/update

# 2. Extract the ZBI from the update package.
print "Extracting the zbi to /tmp/demo/zbi"
tool.zbi.extract --input /tmp/demo/update/zbi --output /tmp/demo/zbi
