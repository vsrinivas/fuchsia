#!/bin/bash
# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

fx-truncate() {
  local -r file="$1"
  local -r size="$2"
  touch "${file}"

  if [[ -z "${size}" ]]; then
    echo >&2 "fx-truncate: size \"${size}\" not given"
    return 1
  fi

  case $(uname) in
    Darwin)
      mkfile -n "${size}" "${file}"
      ;;
    Linux)
      truncate -s "${size}" "${file}"
      ;;
    *)
      head -c "${size}" /dev/zero > "${file}"
      ;;
  esac
  return $?
}

fx-need-mtools() {
  for tool in "mmd" "mcopy"; do
    if ! which "${tool}" >&1 > /dev/null; then
      echo >&2 "Tool \"${tool}\" not found. You may need to install GNU mtools"
      return 1
    fi
  done
}