#!/bin/bash

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Expected environment variables:
#
# "ROOTDIR": the root directory of the new Debian image.
#
# "ARTIFACTDIR": the debos artifact directory.

# Copy out the vmlinuz / initrd images.
cp --dereference --force "${ROOTDIR}/vmlinuz" "${ARTIFACTDIR}/vmlinuz"
cp --dereference --force "${ROOTDIR}/initrd.img" "${ARTIFACTDIR}/initrd.img"

# Now that we've extracted the files, remove all vmlinuz / initrd from /boot to
# save space (~50 MiB) on the final image.
rm --force "${ROOTDIR}"/boot/vmlinuz-*
rm --force "${ROOTDIR}"/boot/initrd.img-*
rm --force "${ROOTDIR}"/vmlinuz
rm --force "${ROOTDIR}"/initrd.img
rm --force "${ROOTDIR}"/vmlinuz.old
rm --force "${ROOTDIR}"/initrd.img.old
