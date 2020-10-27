#!/bin/bash
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -o pipefail

usage() {
    echo "usage: ${0} {file} {reference} {output}"
    echo "Compares two files and writes any differences to an output file."
    echo "Exits with success if the files match, and failure if they do not."
}

diff ${1} ${2} > ${3}
if [ "$?" -eq "1" ]; then
    file_path=`realpath ${2}`
    reference_path=`realpath ${1}`
    1>&2 echo "Error: file \"${1}\" does not match reference \"${2}\"."
    1>&2 echo "\"${2}\" may need to be regenerated, perhaps by running"
    1>&2 echo "  cp ${reference_path} ${file_path}"
    exit -1
fi
