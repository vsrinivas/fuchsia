#!/usr/bin/env bash

# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euo pipefail

PATH_TO_SYSCALLS_DIR="${FUCHSIA_DIR}/third_party/gvisor_syscall_tests"
PATH_TO_TMP="${PATH_TO_SYSCALLS_DIR}/tmp"
PATH_TO_GVISOR="${PATH_TO_SYSCALLS_DIR}/gvisor"

# Preserve the relative structure used by gVisor so the include paths continue to work.
PATH_TO_TEST_SRCS="test/syscalls/linux"
PATH_TO_TEST_UTILS="test/util"

TEST_SRC_FILENAMES="TEST_SRC_FILENAMES.txt"
TEST_UTILS_FILENAMES="TEST_UTILS_FILENAMES.txt"
COMMIT_HASH_FILENAME="LATEST_GIT_COMMIT.txt"
LICENSE_FILENAME="LICENSE"

# Delete both the tmp and gvisor directories so we start from a clean slate.
rm -rf "${PATH_TO_TMP}"
rm -rf "${PATH_TO_GVISOR}"

# Get the full gVisor repository.
git clone https://github.com/google/gvisor.git "${PATH_TO_TMP}"

# Create the test source and util file directories, if they don't yet exist.
mkdir -p "${PATH_TO_GVISOR}/${PATH_TO_TEST_SRCS}"

# Move test source files.
while read -r filename; do
    cp "${PATH_TO_TMP}/${PATH_TO_TEST_SRCS}/${filename}" "${PATH_TO_GVISOR}/${PATH_TO_TEST_SRCS}/${filename}";
done < "${PATH_TO_SYSCALLS_DIR}/${TEST_SRC_FILENAMES}"

# Create the test util file directory, if it doesn't yet exist.
mkdir -p "${PATH_TO_GVISOR}/${PATH_TO_TEST_UTILS}"

# Move test util files.
while read -r filename; do
    cp "${PATH_TO_TMP}/${PATH_TO_TEST_UTILS}/${filename}" "${PATH_TO_GVISOR}/${PATH_TO_TEST_UTILS}/${filename}";
done < "${PATH_TO_SYSCALLS_DIR}/${TEST_UTILS_FILENAMES}"

# Move License file.
cp "${PATH_TO_TMP}/${LICENSE_FILENAME}" "${PATH_TO_SYSCALLS_DIR}/${LICENSE_FILENAME}"

# Save a list of commits being imported.
TEST_SRC_PATHS=$(awk "{print \"${PATH_TO_TEST_SRCS}/\" \$0}" < ${PATH_TO_SYSCALLS_DIR}/${TEST_SRC_FILENAMES})
TEST_UTILS_PATHS=$(awk "{print \"${PATH_TO_TEST_UTILS}/\" \$0}" < ${PATH_TO_SYSCALLS_DIR}/${TEST_UTILS_FILENAMES})

LAST_IMPORTED_COMMIT=$(cat "${PATH_TO_SYSCALLS_DIR}/${COMMIT_HASH_FILENAME}")
IMPORTED=$(git -C "${PATH_TO_TMP}" log --format="+ %C(auto) %h %s" ${LAST_IMPORTED_COMMIT}..HEAD -- ${TEST_SRC_PATHS} ${TEST_UTILS_PATHS})

# Store the hash of the latest commit.
git -C "${PATH_TO_TMP}" rev-parse HEAD > "${PATH_TO_SYSCALLS_DIR}/${COMMIT_HASH_FILENAME}"

# Clean up gVisor.
rm -rf "${PATH_TO_TMP}"

# Print out the imported commits.
echo "Imported changes from the following commits:"
echo ""
echo "$IMPORTED"