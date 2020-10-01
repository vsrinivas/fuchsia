#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

_SSH_IDENTITY='.ssh/fuchsia_ed25519'
_SSH_AUTH_KEYS='.ssh/fuchsia_authorized_keys'

readonly LOCAL_KEY_MISMATCH_ERROR=64
readonly REMOTE_KEY_MISMATCH_ERROR=65
readonly MIXED_KEY_MISMATCH_ERROR=66
readonly KEY_GENERATION_ERROR=67

function fail_with_mixed_mismatch {
  local extra_path_local=""
  local extra_path_remote=""
  if is_local_a_tree; then
    extra_path_local="
    ${local_fuchsia_dir}/.ssh/pkey*
    ${local_fuchsia_dir}/.fx-ssh-path"
  fi
  if is_remote_a_tree; then
    extra_path_remote="
    ${remote_fuchsia_dir}/.ssh/pkey*
    ${remote_fuchsia_dir}/.fx-ssh-path"
  fi

  fx-error "
You have different SSH credentials for Fuchsia devices in your local machine
and on the remote server.

Local and remote ~/.ssh/fuchsia_ed25519 and ~/.ssh/fuchsia_ed25519.pub need to match.

Before you continue, update your local and remote source code.
This ensures that the host tools on both machines expect the SSH credentials
in a consistent location.

If you have been using local tools like 'fx pave-remote' or 'fserve-remote.sh',
the local credentials are probably most recent and you can backup and delete
the remote keys on ${host}:
    ~/.ssh/fuchsia_ed25519*${extra_path_remote}
and then run this command again.

If you want to keep the remote credentials, backup and delete the local keys:
    ~/.ssh/fuchsia_ed25519*${extra_path_local}
and then run this command again.

If you manage your SSH credentials manually, use the --no-check-ssh-keys flag to skip this check.
"
  exit ${MIXED_KEY_MISMATCH_ERROR}
}


# Check if there is a Fuchsia SSH key in HOME (used for the GN SDK)
function normalize_local_gn_key {
  if [[ ! -f "$HOME/${_SSH_IDENTITY}" ]]; then
    return 1
  fi
  return 0
}

# Check whether a Fuchsia SSH key is present on the remote host when
# the remote is not a Fuchsia tree.
function normalize_remote_gn_key {
  # shellcheck disable=SC2029
  ssh ${ssh_base_args[@]+"${ssh_base_args[@]}"} "${host}" \
    "[[ -f \"$_SSH_IDENTITY\" ]]" || return ${_ERROR_NO_KEY}
}

# If a key exists in either //.ssh or HOME/.ssh, copy to the other location
# (normalize) and return 0.
# If no key is found on either location, return 1.
# If keys exist on both locations, return 0 if they are the same, or exit
# with LOCAL_KEY_MISMATCH_ERROR if they are different.
#
function normalize_local_intree_key {
  # run in a subshell to avoid exiting
  (
    "${local_fuchsia_dir}/tools/ssh-keys/gen-ssh-keys.sh" --no-new-key >/dev/null 2>&1
  )
  status=$?
  if [[ $status -eq $_ERROR_NO_KEY ]] ; then
    return 1
  elif [[ $status -eq 0 ]]; then
    return 0
  elif [[ $status -eq $_ERROR_MISMATCHED_KEYS ]]; then
    fx-error "ERROR: mismatched key in local machine."
    fx-error "Run '//tools/ssh-keys/gen-ssh-keys.sh' to learn how to fix it."
    exit $LOCAL_KEY_MISMATCH_ERROR
  else
    fx-error "ERROR: unexpected error code $status from local //tools/ssh-keys/gen-ssh-keys.sh"
    fx-error "Update your local source tree and run '//tools/ssh-keys/gen-ssh-keys.sh --no-new-key' manually to check."
    exit $LOCAL_KEY_MISMATCH_ERROR
  fi
}


# If a key exists in either //.ssh or HOME/.ssh on the remote server, copy to
# the other location (normalize) and return 0.
# If no key is found on either location, return 1.
# If keys exist on both locations, return 0 if they are the same, or exit
# with LOCAL_KEY_MISMATCH_ERROR if they are different.
function normalize_remote_intree_key {
  # run in a subshell to avoid exiting
  (
    # normalize remote keys accross //.ssh and HOME/.ssh using the remote
    # gen-ssh-keys.sh with --no-new-key argument, so it doesn't generate a new
    # key if one doesn't exist.
    # shellcheck disable=SC2029
    ssh ${ssh_base_args[@]+"${ssh_base_args[@]}"} "${host}" \
      "${remote_fuchsia_dir}/tools/ssh-keys/gen-ssh-keys.sh --no-new-key >/dev/null 2>&1"
  )
  status=$?
  if [[ $status -eq $_ERROR_NO_KEY ]] ; then
    return 1
  elif [[ $status -eq 0 ]]; then
    return 0
  elif [[ $status -eq $_ERROR_MISMATCHED_KEYS ]]; then
    fx-error "ERROR: mismatched SSH keys in the remote machine."
    fx-error "Run '//tools/ssh-keys/gen-ssh-keys.sh' on ${host} to learn how to fix it."
    exit $REMOTE_KEY_MISMATCH_ERROR
  elif [[ $status -eq 1 ]]; then
    # error code "1" is from an old gen-ssh-keys.sh source.
    fx-error "ERROR: you have an old //tools/ssh-keys/gen-ssh-keys.sh in ${host}:${remote_fuchsia_dir}."
    fx-error "Please update your remote source tree and run again, or use --no-check-ssh-keys to skip SSH key checking."
    exit $REMOTE_KEY_MISMATCH_ERROR
  else
    fx-error "ERROR: unexpected error code $status from remote //tools/ssh-keys/gen-ssh-keys.sh"
    fx-error "Please update your remote source tree in ${host} and run '//tools/ssh-keys/gen-ssh-keys.sh --no-new-key' manually to check."
    exit $REMOTE_KEY_MISMATCH_ERROR
  fi
}

# Arguments
#   *: Public key comment.
function sync_remote_keys_intree {
  # shellcheck disable=SC2029
  ssh ${ssh_base_args[@]+"${ssh_base_args[@]}"} "${host}" \
    "${remote_fuchsia_dir}/tools/ssh-keys/gen-ssh-keys.sh --description \"$*\""
}

function compare_remote_and_local {
  local -r remote_key="\${HOME}/${_SSH_IDENTITY}"
  local -r local_key="${HOME}/${_SSH_IDENTITY}"
  # shellcheck disable=SC2029
  ssh ${ssh_base_args[@]+"${ssh_base_args[@]}"} "${host}" "cat ${remote_key}" \
    | cmp -s "${local_key}" -
}

function copy_local_to_remote {
  scp ${ssh_base_args[@]+"${ssh_base_args[@]}"} -q -p "${HOME}/${_SSH_IDENTITY}" "${HOME}/${_SSH_IDENTITY}.pub" "${host}:.ssh/"
  # shellcheck disable=SC2029
  ssh ${ssh_base_args[@]+"${ssh_base_args[@]}"} "${host}" "cat >> ${_SSH_AUTH_KEYS}" < "${HOME}/${_SSH_AUTH_KEYS}"
}

function copy_remote_to_local {
  (
    # force subshell to limit scope of umask
    umask 077
    mkdir -p "$HOME/.ssh"
    scp ${ssh_base_args[@]+"${ssh_base_args[@]}"} -q -p "${host}:${_SSH_IDENTITY}" "${host}:${_SSH_IDENTITY}.pub" "$HOME/.ssh"
    umask 133
    # shellcheck disable=SC2029
    ssh ${ssh_base_args[@]+"${ssh_base_args[@]}"} "${host}" cat "${_SSH_AUTH_KEYS}" >> "${HOME}/${_SSH_AUTH_KEYS}"
  )
}

function is_local_a_tree {
  test -n "$local_fuchsia_dir"
}

function is_remote_a_tree {
  test -n "$remote_fuchsia_dir"
}

# Verify if the Fuchsia SSH keys in the current host and on a remote host
# are the same.
#
# If no default identity is available on one of the local or remote machine,
# this script copies that identity into place on the other machine. If
# mismatched identities exist, this script prints an actionable error
# message and returns a status code of 113 (_ERROR_MISMATCHED_KEYS).
#
# Callers should have an user-facing '--no-check-ssh-keys' flag that
# skips this method if the user prefers to manage SSH credentials manually.
#
# Args:
#   1:   local_fuchsia_dir (empty string if GN SDK)
#   2:   Remote host
#   3:   Remote local_fuchsia_dir (empty string if GN SDK)
#   4-*: Additional args for `ssh`

function verify_default_keys {
  local -r local_fuchsia_dir="$1"
  local -r host="$2"
  local -r remote_fuchsia_dir="$3"
  shift 3
  local temp_ssh_args=()
  while [[ $# -gt 0 ]]; do
    if [[ "$1" == "-S" ]]; then
      # "-S" doesn't work for scp, so we transform it to its equivalent -o ControlPath=...
      shift
      if [[ $# -gt 0 ]]; then
        temp_ssh_args+=("-o" "ControlPath=$1")
      fi
    else
      temp_ssh_args+=( $1 )
    fi
    shift
  done

  local -r ssh_base_args=( "${temp_ssh_args[@]}" )

  local has_local_key=false
  local has_remote_key=false

  # keep in sync with //tools/ssh-keys/gen-ssh-keys.sh
  local -r _ERROR_NO_KEY=112
  local -r _ERROR_MISMATCHED_KEYS=113
  # // keep in sync with //tools/ssh-keys/gen-ssh-keys.sh


  if is_local_a_tree; then
    if normalize_local_intree_key; then
      has_local_key=true
    fi
  else
    if normalize_local_gn_key; then
      has_local_key=true
    fi
  fi

  if is_remote_a_tree; then
    if normalize_remote_intree_key; then
      has_remote_key=true
    fi
  else
    if normalize_remote_gn_key; then
      has_remote_key=true
    fi
  fi

  if ! $has_local_key && ! $has_remote_key; then
    if is_remote_a_tree; then
      fx-warn "No SSH credentials found, generating on the remote server first."
      sync_remote_keys_intree "triggered by $0 from $(hostname -f)" \
        || exit $KEY_GENERATION_ERROR
      has_remote_key=true
    else
      # if remote is not a Fuchsia tree, it is easier to generate the
      # key locally and copy to the remote than the opposite.
      fx-warn "No SSH credentials found, generating one locally first."
      # check-fuchsia-ssh-config is defined in fuchsia-common.sh,
      # assume it is already loaded by the caller
      check-fuchsia-ssh-config || exit $KEY_GENERATION_ERROR
      has_local_key=true
    fi
  fi

  if $has_local_key && $has_remote_key ; then
    # check if they match
    compare_remote_and_local || fail_with_mixed_mismatch
    return 0

  elif $has_local_key && ! $has_remote_key; then
    fx-warn "Copying local SSH credentials to the remote server"
    copy_local_to_remote
    if is_remote_a_tree; then
      sync_remote_keys_intree
    fi

  elif ! $has_local_key && $has_remote_key; then
    fx-warn "Copying remote SSH credentials to the local machine"
    copy_remote_to_local
    if is_local_a_tree; then
      "${local_fuchsia_dir}/tools/ssh-keys/gen-ssh-keys.sh"
    fi
  fi
}
