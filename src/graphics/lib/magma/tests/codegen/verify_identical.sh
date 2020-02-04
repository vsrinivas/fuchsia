#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -eo pipefail

usage() {
    echo "usage: ${0} {file} {reference} {output}"
    echo "Compares two files and writes any differences to an output file."
    echo "Exits with success if the files match, and failure if they do not."
}

diff ${1} ${2} > ${3}
if [ "$?" -eq "1" ]; then
    1>&2 echo "Error: file \"${1}\" does not match reference \"${2}\"."
    1>&2 echo "\"${1}\" may need to be regenerated."
    exit -1
fi
