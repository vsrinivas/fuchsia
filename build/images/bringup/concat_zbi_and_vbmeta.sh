#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Fuchsia's implementation of fastboot boot on locked bootloaders expects the
# provided image to be a concatenation of the ZBI and Vbmeta. This script
# is a wrapper around a custom siging script that performs this concatenation.
ZBI=
VBMETA=
CUSTOM_SIGNING_SCRIPT=
SIGNING_SCRIPT_ARGS=
OUTPUT=

while [[ $# -gt 0 ]];
do
  key=$1
  case $key in
    -z)
    ZBI="${2}"
    shift
    shift
    ;;
    -o)
    OUTPUT=${2}
    shift
    shift
    ;;
    -v)
    VBMETA="${2}"
    shift
    shift
    ;;
    -s)
    CUSTOM_SIGNING_SCRIPT="${2}"
    shift
    shift
    ;;
    *)
    SIGNING_SCRIPT_ARGS+="$key"
    shift
  esac
done

if [ ! -z $CUSTOM_SIGNING_SCRIPT ]
then
  $CUSTOM_SIGNING_SCRIPT -z $ZBI -o $OUTPUT $SIGNING_SCRIPT_ARGS
  ZBI=$OUTPUT
fi

if [ ! -z $VBMETA ]
then
  cat $ZBI $VBMETA > $OUTPUT
else
  cp $ZBI $OUTPUT
fi
