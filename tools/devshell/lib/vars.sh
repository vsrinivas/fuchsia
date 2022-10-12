# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -n "${ZSH_VERSION:-}" ]]; then
  devshell_lib_dir=${${(%):-%x}:a:h}
else
  devshell_lib_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
fi

FUCHSIA_DIR="$(dirname "$(dirname "$(dirname "${devshell_lib_dir}")")")"
export FUCHSIA_DIR
export FUCHSIA_OUT_DIR="${FUCHSIA_OUT_DIR:-${FUCHSIA_DIR}/out}"
source "${devshell_lib_dir}/platform.sh"
source "${devshell_lib_dir}/fx-cmd-locator.sh"
source "${devshell_lib_dir}/fx-optional-features.sh"
unset devshell_lib_dir

# Subcommands can use this directory to cache artifacts and state that should
# persist between runs. //scripts/fx ensures that it exists.
#
# fx commands that make use of this directory should include the command name
# in the names of any cached artifacts to make naming collisions less likely.
export FX_CACHE_DIR="${FUCHSIA_DIR}/.fx"

if [[ "${FUCHSIA_DEVSHELL_VERBOSITY:-0}" -eq 1 ]]; then
  set -x
fi

# For commands whose subprocesses may use RBE, prefix those commands
# conditioned on 'if fx-rbe-enabled' (function).
# This could not be made into a shell-function because it is used
# as both a function and non-built-in command, and functions do not compose
# by prefixing in shell.
RBE_WRAPPER=( "$FUCHSIA_DIR"/build/rbe/fuchsia-reproxy-wrap.sh -- )

# Use this to conditionally prefix a command with "${RBE_WRAPPER[@]}".
# NOTE: this function depends on FUCHSIA_BUILD_DIR which is set only after
# initialization.
function fx-rbe-enabled {
  fx-build-dir-if-present || {
    fx-error "No build directory found."
    fx-error "Run \"fx set\" to create a new build directory, or specify one with --dir"
    exit 1
  }
  if grep -q -w -e "enable_rbe" "${FUCHSIA_BUILD_DIR}/args.gn" ; then
    fx-warn "The 'enable_rbe' GN arg has been renamed to 'rust_rbe_enable'."
    fx-warn "Please update your ${FUCHSIA_BUILD_DIR}/args.gn file (fx args)."
  fi
  # If RBE is enabled for any language, then the whole build needs to be
  # wrapped with ${RBE_WRAPPER[@]}.
  grep -q -e "^[ \t]*rust_rbe_enable[ ]*=[ ]*true" \
    -e "^[ \t]*cxx_rbe_enable[ ]*=[ ]*true" \
    -e "^[ \t]*enable_rbe[ ]*=[ ]*true" \
    "${FUCHSIA_BUILD_DIR}/args.gn"
}

# At the moment, direct use of RBE uses gcloud to authenticate.
# TODO(fxbug.dev/91579): simplify this process with an auth script
#   without depending on a locally installed gcloud.
function fx-check-rbe-setup {
  if ! fx-rbe-enabled ; then return ; fi

  gcloud="$(which gcloud)" || {
    cat <<EOF

\`gcloud\` command not found (but is needed to authenticate).
\`gcloud\` can be installed from the Cloud SDK:

  http://go/cloud-sdk#installing-and-using-the-cloud-sdk

EOF
  return
  }

# Check authentication first.
# Instruct user to authenticate if needed.
  "$gcloud" auth list 2>&1 | grep -q "$USER@google.com" || {
    cat <<EOF
Did not find credentialed account (\`gcloud auth list\`): $USER@google.com.

To authenticate, run:

  gcloud auth login --update-adc --no-browser

EOF
}

}

# fx-is-stderr-tty exits with success if stderr is a tty.
function fx-is-stderr-tty {
  [[ -t 2 ]]
}

# fx-info prints a line to stderr with a blue INFO: prefix.
function fx-info {
  if fx-is-stderr-tty; then
    echo -e >&2 "\033[1;34mINFO:\033[0m $*"
  else
    echo -e >&2 "INFO: $*"
  fi
}

# fx-warn prints a line to stderr with a yellow WARNING: prefix.
function fx-warn {
  if fx-is-stderr-tty; then
    echo -e >&2 "\033[1;33mWARNING:\033[0m $*"
  else
    echo -e >&2 "WARNING: $*"
  fi
}

# fx-error prints a line to stderr with a red ERROR: prefix.
function fx-error {
  if fx-is-stderr-tty; then
    echo -e >&2 "\033[1;31mERROR:\033[0m $*"
  else
    echo -e >&2 "ERROR: $*"
  fi
}

function fx-gn {
  PATH="${PREBUILT_PYTHON3_DIR}/bin:${PATH}" "${PREBUILT_GN}" "$@"
}

function fx-is-bringup {
  grep '^[^#]*import("//products/bringup.gni")' "${FUCHSIA_BUILD_DIR}/args.gn" >/dev/null 2>&1
}

function fx-gen-internal {
  # which of `gn gen` and `gn args` we're doing
  local -r subcommand="$1"
  shift

  # If a user executes gen from a symlinked directory that is not a
  # subdirectory $FUCHSIA_DIR then dotgn search may fail, so execute
  # the gen from the $FUCHSIA_DIR.
  (
    cd "${FUCHSIA_DIR}" && \
    fx-gn "${subcommand}" --fail-on-unused-args --check=system --export-rust-project --ninja-executable="${PREBUILT_NINJA}" "${FUCHSIA_BUILD_DIR}" "$@"
  ) || return $?
  # symlink rust-project.json to root of project
  if [[ -f "${FUCHSIA_BUILD_DIR}/rust-project.json" ]]; then
    ln -f -s "${FUCHSIA_BUILD_DIR}/rust-project.json" "${FUCHSIA_DIR}/rust-project.json"
  fi
}

function fx-gen {
  fx-gen-internal gen "$@"
}

function fx-gn-args {
  fx-gen-internal args "$@"
}

function fx-build-config-load {
  # Paths are relative to FUCHSIA_DIR unless they're absolute paths.
  if [[ "${FUCHSIA_BUILD_DIR:0:1}" != "/" ]]; then
    FUCHSIA_BUILD_DIR="${FUCHSIA_DIR}/${FUCHSIA_BUILD_DIR}"
  fi

  if [[ ! -f "${FUCHSIA_BUILD_DIR}/fx.config" ]]; then
    if [[ ! -f "${FUCHSIA_BUILD_DIR}/args.gn" ]]; then
      fx-error "Build directory missing or removed. (${FUCHSIA_BUILD_DIR})"
      fx-error "run \"fx set\", or specify a build dir with --dir or \"fx use\""
      return 1
    fi

    fx-gen || return 1
  fi

  if ! source "${FUCHSIA_BUILD_DIR}/fx.config"; then
    fx-error "${FUCHSIA_BUILD_DIR}/fx.config caused internal error"
    return 1
  fi

  # The source of `fx.config` will re-set the build dir to relative, so we need
  # to abspath it again.
  if [[ "${FUCHSIA_BUILD_DIR:0:1}" != "/" ]]; then
    FUCHSIA_BUILD_DIR="${FUCHSIA_DIR}/${FUCHSIA_BUILD_DIR}"
  fi


  export FUCHSIA_BUILD_DIR FUCHSIA_ARCH

  # TODO(mangini): revisit once fxbug.dev/38436 is fixed.
  if [[ "${HOST_OUT_DIR:0:1}" != "/" ]]; then
    HOST_OUT_DIR="${FUCHSIA_BUILD_DIR}/${HOST_OUT_DIR}"
  fi

  return 0
}

function fx-build-dir-if-present {
  if [[ -n "${_FX_BUILD_DIR:-}" ]]; then
    export FUCHSIA_BUILD_DIR="${_FX_BUILD_DIR}"
  else
    if [[ ! -f "${FUCHSIA_DIR}/.fx-build-dir" ]]; then
      return 1
    fi

    # .fx-build-dir contains $FUCHSIA_BUILD_DIR
    FUCHSIA_BUILD_DIR="$(<"${FUCHSIA_DIR}/.fx-build-dir")"
    if [[ -z "${FUCHSIA_BUILD_DIR}" ]]; then
      return 1
    fi
    # Paths are relative to FUCHSIA_DIR unless they're absolute paths.
    if [[ "${FUCHSIA_BUILD_DIR:0:1}" != "/" ]]; then
      FUCHSIA_BUILD_DIR="${FUCHSIA_DIR}/${FUCHSIA_BUILD_DIR}"
    fi
  fi
  return 0
}

function fx-config-read {
  if ! fx-build-dir-if-present; then
    fx-error "No build directory found."
    fx-error "Run \"fx set\" to create a new build directory, or specify one with --dir"
    exit 1
  fi

  fx-build-config-load || exit $?

  _FX_LOCK_FILE="${FUCHSIA_BUILD_DIR}.build_lock"
}

function fx-change-build-dir {
  local build_dir="$1"

  local -r tempfile="$(mktemp)"
  echo "${build_dir}" > "${tempfile}"
  mv -f "${tempfile}" "${FUCHSIA_DIR}/.fx-build-dir"

  # Now update the environment and root-symlinked build artifacts to reflect
  # the change.
  fx-config-read

  local -r linked_artifacts=("compile_commands.json" "rust-project.json")
  for artifact in "${linked_artifacts[@]}" ; do
    if [[ -f "${FUCHSIA_BUILD_DIR}/$artifact" ]]; then
      ln -sf "${FUCHSIA_BUILD_DIR}/$artifact" "${FUCHSIA_DIR}/$artifact"
    fi
  done
}

function get-device-pair {
  # Uses a file outside the build dir so that it is not removed by `gn clean`
  local pairfile="${FUCHSIA_BUILD_DIR}.device"
  # If .device file exists, use that
  if [[ -f "${pairfile}" ]]; then
    echo "$(<"${pairfile}")"
    return 0
  fi
  return 1
}

function get-device-raw {
  fx-config-read
  local device=""
  # If DEVICE_NAME was passed in fx -d, use it
  if [[ "${FUCHSIA_DEVICE_NAME+isset}" == "isset" ]]; then
    if is-remote-workflow-device && [[ -z "${FX_REMOTE_INVOCATION}" ]]; then
      fx-warn "The -d flag does not work on this end of the remote workflow"
      fx-warn "in order to adjust target devices in the remote workflow, use"
      fx-warn "-d or fx set-device on the local side of the configuration"
      fx-warn "and restart serve-remote"
    fi
    device="${FUCHSIA_DEVICE_NAME}"
  else
    device=$(get-device-pair)
  fi
  if ! is-valid-device "${device}"; then
    fx-error "Invalid device name or address: '${device}'. Some valid examples are:
      strut-wind-ahead-turf, 192.168.3.1:8022, [fe80::7:8%eth0], [fe80::7:8%eth0]:5222, [::1]:22"
    exit 1
  fi
  echo "${device}"
}

function is-valid-device {
  local device="$1"
  if [[ -n "${device}" ]] \
      && ! _looks_like_ipv4 "${device}" \
      && ! _looks_like_ipv6 "${device}" \
      && ! _looks_like_hostname "${device}"; then
    return 1
  fi
}

# Shared among a few subcommands to configure and identify a remote forward
# target for a device.
export _FX_REMOTE_WORKFLOW_DEVICE_ADDR='[::1]:8022'

function is-remote-workflow-device {
  [[ $(get-device-pair) == "${_FX_REMOTE_WORKFLOW_DEVICE_ADDR}" ]]
}

# fx-export-device-address is "public API" to commands that wish to
# have the exported variables set.
function fx-export-device-address {
  FX_DEVICE_NAME="$(get-device-name)"
  FX_DEVICE_ADDR="$(get-fuchsia-device-addr)"
  FX_SSH_ADDR="$(get-device-addr-resource)"
  FX_SSH_PORT="$(get-device-ssh-port)"
  export FX_DEVICE_NAME FX_DEVICE_ADDR FX_SSH_ADDR FX_SSH_PORT
}

function get-device-ssh-port {
  local device
  device="$(get-device-raw)" || exit $?
  local port=""
  # extract port, if present
  if [[ "${device}" =~ :([0-9]+)$ ]]; then
    port="${BASH_REMATCH[1]}"
  fi
  echo "${port}"
}

function get-device-name {
  local device
  device="$(get-device-raw)" || exit $?
  # remove ssh port if present
  if [[ "${device}" =~ ^(.*):[0-9]{1,5}$ ]]; then
    device="${BASH_REMATCH[1]}"
  fi
  echo "${device}"
}

function _looks_like_hostname {
  [[ "$1" =~ ^([a-z0-9][.a-z0-9-]*)?(:[0-9]{1,5})?$ ]] || return 1
}

function _looks_like_ipv4 {
  [[ "$1" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}(:[0-9]{1,5})?$ ]] || return 1
}

function _looks_like_ipv6 {
  [[ "$1" =~ ^\[([0-9a-fA-F:]+(%[0-9a-zA-Z-]{1,})?)\](:[0-9]{1,5})?$ ]] || return 1
  local colons="${BASH_REMATCH[1]//[^:]}"
  # validate that there are no more than 7 colons
  [[ "${#colons}" -le 7 ]] || return 1
}

function _print_ssh_warning {
  fx-warn "Cannot load device SSH credentials. $*"
  fx-warn "Run 'tools/ssh-keys/gen-ssh-keys.sh' to regenerate."
}

# Checks if default SSH keys are missing.
#
# The argument specifies which line of the manifest to retrieve and verify for
# existence.
#
# "key": The SSH identity file (private key). Append ".pub" for the
#   corresponding public key.
# "auth": The authorized_keys file.
function _get-ssh-key {
  local -r _SSH_MANIFEST="${FUCHSIA_DIR}/.fx-ssh-path"

  local filepath
  local -r which="$1"
  if [[ ! "${which}" =~ ^(key|auth)$ ]]; then
    fx-error "_get-ssh-key: invalid argument '$1'. Must be either 'key' or 'auth'"
    exit 1
  fi

  if [[ ! -f "${_SSH_MANIFEST}" ]]; then
    _print_ssh_warning "File not found: ${_SSH_MANIFEST}."
    return 1
  fi

  # Set -r flag to avoid interpreting backslashes as escape characters, e.g. it
  # won't convert "\n" to a newline character.
  { read -r privkey && read -r authkey; } < "${_SSH_MANIFEST}"

  if [[ -z $privkey || -z $authkey ]]; then
    _print_ssh_warning "Manifest file ${_SSH_MANIFEST} is malformed."
    return 1
  fi

  if [[ $which == "auth" ]]; then
    filepath="${authkey}"
  elif [[ $which == "key" ]]; then
    filepath="${privkey}"
  fi

  echo "${filepath}"

  if [[ ! -f "${filepath}" ]]; then
    _print_ssh_warning "File not found: ${filepath}."
    return 1
  fi

  return 0
}

# Invoke the ffx tool. This function invokes ffx via the tools/devshell/ffx
# wrapper that passes configuration information such as default target and build
# directory locations to the ffx tool as needed to provide a seamless fx/ffx
# integration.
function ffx {
  fx-command-run ffx "$@"
}

# Prints path to the default SSH key. These credentials are created by a
# jiri hook.
#
# The corresponding public key is stored in "$(get-ssh-privkey).pub".
function get-ssh-privkey {
  _get-ssh-key key
}

# Prints path to the default authorized_keys to include on Fuchsia devices.
function get-ssh-authkeys {
  _get-ssh-key auth
}

function fx-target-finder-resolve {
  if [[ $# -ne 1 ]]; then
    fx-error "Invalid arguments to fx-target-finder-resolve: [$*]"
    return 1
  fi
  ffx target list --format a "$1"
}

function fx-target-finder-list {
  ffx target list --format a
}

function fx-target-finder-info {
  ffx target list --format s
}

function fx-target-ssh-address {
  ffx target get-ssh-address
}

function multi-device-fail {
  local output devices
  fx-error "Multiple devices found."
  fx-error "Please specify one of the following devices using either \`fx -d <device-name>\` or \`fx set-device <device-name>\`."
  devices="$(fx-target-finder-info)" || {
    code=$?
    fx-error "Device discovery failed with status: $code"
    exit $code
  }
  while IFS="" read -r line; do
    fx-error "\t${line}"
  done < <(printf '%s\n' "${devices}")
  exit 1
}

function get-fuchsia-device-addr {
  fx-config-read
  local device
  device="$(get-device-name)" || exit $?

  # Treat IPv4 addresses in the device name as an already resolved
  # device address.
  if _looks_like_ipv4 "${device}"; then
    echo "${device}"
    return
  fi
  if _looks_like_ipv6 "${device}"; then
    # remove brackets
    device="${device%]}"
    device="${device#[}"
    echo "${device}"
    return
  fi

  local output devices
  case "$device" in
    "")
        output="$(fx-target-finder-list)" || {
          code=$?
          fx-error "Device discovery failed with status: $code"
          exit $code
        }
        if [[ "$(echo "${output}" | wc -l)" -gt "1" ]]; then
          multi-device-fail
        fi
        echo "${output}" ;;
     *) fx-target-finder-resolve "$device" ;;
  esac
}

function get-fuchsia-device-port {
  fx-config-read
  local port
  port="$(get-device-ssh-port)" || exit $?

  if [[ -z "${port}" ]]; then
    local device
    device="$(fx-target-ssh-address)" || {
      code=$?
      fx-error "Device discovery failed with status: ${code}"
      exit ${code}
    }
    if [[ "$(echo "${device}" | wc -l)" -gt "1" ]]; then
      multi-device-fail
    fi
    if [[ "${device}" =~ :([0-9]+)$ ]]; then
      port="${BASH_REMATCH[1]}"
    fi
  fi
  echo "${port}"
}

# get-device-addr-resource returns an address that is properly encased
# for resource addressing for tools that expect that. In practical
# terms this just means encasing the address in square brackets if it
# is an ipv6 address. Note: this is not URL safe as-is, use the -url
# variant instead for URL formulation.
function get-device-addr-resource {
  local addr
  addr="$(get-fuchsia-device-addr)" || exit $?
  if _looks_like_ipv4 "${addr}"; then
    echo "${addr}"
    return 0
  fi

  echo "[${addr}]"
}

function get-device-addr-url {
  get-device-addr-resource | sed 's#%#%25#'
}

function fx-command-run {
  local -r command_name="$1"
  local command_path
  # Use an array because the command may be multiple elements, if it comes from
  # a .fx metadata file.
  command_path=( $(find_executable "${command_name}") )

  if [[ $? -ne 0 || ! -x "${command_path[0]}" ]]; then
    fx-error "Unknown command ${command_name}"
    exit 1
  fi

  shift
  env FX_CALLER="$0" "${command_path[@]}" "$@"
}

function fx-command-exec {
  local -r command_name="$1"
  local command_path
  # Use an array because the command may be multiple elements, if it comes from
  # a .fx metadata file.
  command_path=( $(find_executable "${command_name}") )

  if [[ $? -ne 0 || ! -x "${command_path[0]}" ]]; then
    fx-error "Unknown command ${command_name}"
    exit 1
  fi

  shift
  exec env FX_CALLER="$0" "${command_path[@]}" "$@"
}

function fx-print-command-help {
  local command_path="$1"
  if grep '^## ' "$command_path" > /dev/null; then
    sed -n -e 's/^## //p' -e 's/^##$//p' < "$command_path"
  else
    local -r command_name=$(basename "$command_path" ".fx")
    echo "No help found. Try \`fx $command_name -h\`"
  fi
}

function fx-command-help {
  fx-print-command-help "$0"
  echo -e "\nFor global options, try \`fx help\`."
}


# This function massages arguments to an fx subcommand so that a single
# argument `--switch=value` becomes two arguments `--switch` `value`.
# This lets each subcommand's main function use simpler argument parsing
# while still supporting the preferred `--switch=value` syntax.  It also
# handles the `--help` argument by redirecting to what `fx help command`
# would do.  Because of the complexities of shell quoting and function
# semantics, the only way for this function to yield its results
# reasonably is via a global variable.  FX_ARGV is an array of the
# results.  The standard boilerplate for using this looks like:
#   function main {
#     fx-standard-switches "$@"
#     set -- "${FX_ARGV[@]}"
#     ...
#     }
# Arguments following a `--` are also added to FX_ARGV but not split, as they
# should usually be forwarded as-is to subprocesses.
function fx-standard-switches {
  # In bash 4, this can be `declare -a -g FX_ARGV=()` to be explicit
  # about setting a global array.  But bash 3 (shipped on macOS) does
  # not support the `-g` flag to `declare`.
  FX_ARGV=()
  while [[ $# -gt 0 ]]; do
    if [[ "$1" = "--help" || "$1" = "-h" ]]; then
      fx-print-command-help "$0"
      # Exit rather than return, so we bail out of the whole command early.
      exit 0
    elif [[ "$1" == --*=* ]]; then
      # Turn --switch=value into --switch value.
      FX_ARGV+=("${1%%=*}" "${1#*=}")
    elif [[ "$1" == "--" ]]; then
      # Do not parse remaining parameters after --
      FX_ARGV+=("$@")
      return
    else
      FX_ARGV+=("$1")
    fi
    shift
  done
}

function fx-choose-build-concurrency {
  # If any remote execution is enabled (e.g. via Goma or RBE),
  # allow ninja to launch many more concurrent actions than what local
  # resources can support.
  # This covers GN args: cxx_rbe_enable, rust_rbe_enable.
  # Kludge: grep-ing the args.gn like this is admittedly brittle, and prone
  # to error when we enable remote execution on new tools.
  if grep -q -e "use_goma = true" \
      -e "_rbe_enable = true" \
      "${FUCHSIA_BUILD_DIR}/args.gn"; then
    # The recommendation from the Goma team is to use 10*cpu-count for C++.
    local cpus
    cpus="$(fx-cpu-count)"
    echo $((cpus * 10))
  else
    fx-cpu-count
  fi
}

function fx-cpu-count {
  local -r cpu_count=$(getconf _NPROCESSORS_ONLN)
  echo "$cpu_count"
}


# Use a lock file around a command if possible.
# Print a message if the lock isn't immediately entered,
# and block until it is.
function fx-try-locked {
  if [[ -z "${_FX_LOCK_FILE}" ]]; then
    fx-error "fx internal error: attempt to run locked command before fx-config-read"
    exit 1
  fi
  if ! command -v shlock >/dev/null; then
    # Can't lock! Fall back to unlocked operation.
    fx-exit-on-failure "$@"
  elif shlock -f "${_FX_LOCK_FILE}" -p $$; then
    # This will cause a deadlock if any subcommand calls back to fx build,
    # because shlock isn't reentrant by forked processes.
    fx-cmd-locked "$@"
  else
    echo "Locked by ${_FX_LOCK_FILE}..."
    while ! shlock -f "${_FX_LOCK_FILE}" -p $$; do sleep .1; done
    fx-cmd-locked "$@"
  fi
}

function fx-cmd-locked {
  if [[ -z "${_FX_LOCK_FILE}" ]]; then
    fx-error "fx internal error: attempt to run locked command before fx-config-read"
    exit 1
  fi
  # Exit trap to clean up lock file. Intentionally use the current value of
  # $_FX_LOCK_FILE rather than the value at the time that trap executes to
  # ensure we delete the original file even if the value of $_FX_LOCK_FILE
  # changes for whatever reason.
  trap "[[ -n \"${_FX_LOCK_FILE}\" ]] && rm -f \"${_FX_LOCK_FILE}\"" EXIT
  fx-exit-on-failure "$@"
}

function fx-exit-on-failure {
  "$@" || exit $?
}

# Massage a ninja command line to add default -j and/or -l switches.
# Arguments:
#    print_full_cmd   if true, prints the full ninja command line before
#                     executing it
#    ninja command    the ninja command itself. This can be used both to run
#                     ninja directly or to run a wrapper script around ninja.
function fx-run-ninja {
  # Separate the command from the arguments so we can prepend default -j/-l
  # switch arguments.  They need to come before the user's arguments in case
  # those include -- or something else that makes following arguments not be
  # handled as normal switches.
  local print_full_cmd="$1"
  shift
  local cmd="$1"
  shift

  local args=()
  local full_cmdline
  local have_load=false
  local have_jobs=false
  while [[ $# -gt 0 ]]; do
    case "$1" in
    -l) have_load=true ;;
    -j) have_jobs=true ;;
    esac
    args+=("$1")
    shift
  done

  if ! "$have_load"; then
    if [[ "$(uname -s)" == "Darwin" ]]; then
      # Load level on Darwin is quite different from that of Linux, wherein a
      # load level of 1 per CPU is not necessarily a prohibitive load level. An
      # unscientific study of build side effects suggests that cpus*20 is a
      # reasonable value to prevent catastrophic load (i.e. user can not kill
      # the build, can not lock the screen, etc).
      local cpus
      cpus="$(fx-cpu-count)"
      args=("-l" $((cpus * 20)) "${args[@]}")
    fi
  fi

  if ! "$have_jobs"; then
    local concurrency
    concurrency="$(fx-choose-build-concurrency)"
    # macOS in particular has a low default for number of open file descriptors
    # per process, which is prohibitive for higher job counts. Here we raise
    # the number of allowed file descriptors per process if it appears to be
    # low in order to avoid failures due to the limit. See `getrlimit(2)` for
    # more information.
    local min_limit=$((concurrency * 2))
    if [[ $(ulimit -n) -lt "${min_limit}" ]]; then
      ulimit -n "${min_limit}"
    fi
    args=("-j" "${concurrency}" "${args[@]}")
  fi

  # Check for a bad element in $PATH.
  # We build tools in the build, such as touch(1), targeting Fuchsia. Those
  # tools end up in the root of the build directory, which is also $PWD for
  # tool invocations. As we don't have hermetic locations for all of these
  # tools, when a user has an empty/pwd path component in their $PATH,
  # the Fuchsia target tool will be invoked, and will fail.
  # Implementation detail: Normally you would split path with IFS or a similar
  # strategy, but catching the case where the first or last components are
  # empty can be tricky in that case, so the pattern match strategy here covers
  # the cases more easily. We check for three cases: empty prefix, empty suffix
  # and empty inner.
  case "${PATH}" in
  :*|*:|*::*)
    fx-error "Your \$PATH contains an empty element that will result in build failure."
    fx-error "Remove the empty element from \$PATH and try again."
    echo "${PATH}" | grep --color -E '^:|::|:$' >&2
    exit 1
  ;;
  .:*|*:.|*:.:*)
    fx-error "Your \$PATH contains the working directory ('.') that will result in build failure."
    fx-error "Remove the '.' element from \$PATH and try again."
    echo "${PATH}" | grep --color -E '^.:|:.:|:.$' >&2
    exit 1
  ;;
  esac


  # TERM is passed for the pretty ninja UI
  # PATH is passed as some tools are referenced via $PATH due to platform differences.
  # TMPDIR is passed for Goma on macOS.
  # NINJA_STATUS is passed to control Ninja progress status.
  # GOMA_DISABLED is passed to forcefully disabling Goma.
  #
  # GOMA_DISABLED and TMPDIR must be set, or unset, not empty. Some Dart
  # build tools have been observed writing into source paths
  # when TMPDIR="" - it is deliberately unquoted and using the ${+} expansion
  # expression). GOMA_DISABLED will forcefully disable Goma even if it's set to
  # empty.
  #
  # rbe_wrapper is used to auto-start/stop a proxy process for the duration of
  # the build, so that RBE-enabled build actions can operate through the proxy.
  #
  local newpath="${PREBUILT_PYTHON3_DIR}/bin:${PATH}"
  local rbe_wrapper=()
  if fx-rbe-enabled ; then rbe_wrapper=("${RBE_WRAPPER[@]}") ; fi
  full_cmdline=(env -i "TERM=${TERM}" "PATH=${newpath}" \
    ${NINJA_STATUS+"NINJA_STATUS=${NINJA_STATUS}"} \
    ${GOMA_DISABLED+"GOMA_DISABLED=$GOMA_DISABLED"} \
    ${TMPDIR+"TMPDIR=$TMPDIR"} \
    ${CLICOLOR_FORCE+"CLICOLOR_FORCE=$CLICOLOR_FORCE"} \
    "${rbe_wrapper[@]}" \
    "$cmd" "${args[@]}")

  if [[ "${print_full_cmd}" = true ]]; then
    echo "${full_cmdline[@]}"
    echo
  fi
  fx-try-locked "${full_cmdline[@]}"
}

function fx-get-image {
  fx-command-run list-build-artifacts --name "$1" --type "$2" --expect-one images
}

function fx-get-zbi {
  fx-get-image "$1" zbi
}

function fx-get-qemu-kernel {
  fx-get-image qemu-kernel kernel
}

function fx-zbi {
  "${FUCHSIA_BUILD_DIR}/$(fx-command-run list-build-artifacts --name zbi --expect-one tools)" --compressed="$FUCHSIA_ZBI_COMPRESSION" "$@"
}

function fx-zbi-default-compression {
  "${FUCHSIA_BUILD_DIR}/$(fx-command-run list-build-artifacts --name zbi --expect-one tools)" "$@"
}
