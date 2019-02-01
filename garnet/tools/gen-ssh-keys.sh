#!/usr/bin/env bash
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

readonly SCRIPT_ROOT="$(cd $(dirname ${BASH_SOURCE[0]} ) && pwd)"
readonly FUCHSIA_ROOT="${SCRIPT_ROOT}/../.."
readonly SSH_DIR="${FUCHSIA_ROOT}/.ssh"
readonly authfile="${SSH_DIR}/authorized_keys"
readonly keyfile="${SSH_DIR}/pkey"

mkdir -p "${SSH_DIR}"
if [[ ! -f "${authfile}" ]]; then
  if [[ ! -f "${keyfile}" ]]; then
    ssh-keygen -P "" -t ed25519 -f "${keyfile}" -C "$USER@$(hostname -f)"
  fi

  ssh-keygen -y -f "${keyfile}" > "${authfile}"
fi