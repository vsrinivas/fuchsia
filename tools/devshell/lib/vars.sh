# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -n "${ZSH_VERSION}" ]]; then
  devshell_lib_dir=${${(%):-%x}:a:h}
else
  devshell_lib_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
fi

export FUCHSIA_DIR="$(dirname $(dirname $(dirname "${devshell_lib_dir}")))"
export FUCHSIA_OUT_DIR="${FUCHSIA_OUT_DIR:-${FUCHSIA_DIR}/out}"
unset devshell_lib_dir

if [[ "${FUCHSIA_DEVSHELL_VERBOSITY}" -eq 1 ]]; then
  set -x
fi

# fx-warn prints a line to stderr with a yellow WARNING: prefix.
function fx-warn {
  if [[ -t 2 ]]; then
    echo -e >&2 "\033[1;33mWARNING:\033[0m $@"
  else
    echo -e >&2 "WARNING: $@"
  fi
}

# fx-error prints a line to stderr with a red ERROR: prefix.
function fx-error {
  if [[ -t 2 ]]; then
    echo -e >&2 "\033[1;31mERROR:\033[0m $@"
  else
    echo -e >&2 "ERROR: $@"
  fi
}

function fx-symbolize {
  if [[ -z "$FUCHSIA_BUILD_DIR" ]]; then
    fx-config-read
  fi
  if [[ -z "$BUILDTOOLS_CLANG_DIR" ]]; then
    source "${FUCHSIA_DIR}/buildtools/vars.sh"
  fi
  local idstxt="${FUCHSIA_BUILD_DIR}/ids.txt"
  if [[ $# -gt 0 ]]; then
    idstxt="$1"
  fi
  local prebuilt_dir="${FUCHSIA_DIR}/zircon/prebuilt/downloads"
  local llvm_symbolizer="${BUILDTOOLS_CLANG_DIR}/bin/llvm-symbolizer"
  "${prebuilt_dir}/symbolize" -ids-rel -ids "$idstxt" -llvm-symbolizer "$llvm_symbolizer"
}

function fx-gen {
    (
      set -ex
      "${FUCHSIA_DIR}/scripts/build-zircon.sh" -g
      "${FUCHSIA_DIR}/buildtools/gn" gen "${FUCHSIA_BUILD_DIR}"
    ) || return 1
}

function fx-build-config-load {
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

  # Paths are relative to FUCHSIA_DIR unless they're absolute paths.
  if [[ "${FUCHSIA_BUILD_DIR:0:1}" != "/" ]]; then
    FUCHSIA_BUILD_DIR="${FUCHSIA_DIR}/${FUCHSIA_BUILD_DIR}"
  fi

  export FUCHSIA_BUILD_DIR FUCHSIA_ARCH

  export ZIRCON_BUILDROOT="${ZIRCON_BUILDROOT:-${FUCHSIA_OUT_DIR}/build-zircon}"
  export ZIRCON_BUILD_DIR="${ZIRCON_BUILD_DIR:-${ZIRCON_BUILDROOT}/build-${FUCHSIA_ARCH}}"
  export ZIRCON_TOOLS_DIR="${ZIRCON_BUILDROOT}/tools"

  return 0
}

function fx-build-dir-if-present {
  if [[ -n "${_FX_BUILD_DIR}" ]]; then
    export FUCHSIA_BUILD_DIR="${_FX_BUILD_DIR}"
  else
    if [[ ! -f "${FUCHSIA_DIR}/.fx-build-dir" ]]; then

      # Old path, try to load old default config and migrate it:
      if [[ -f "${FUCHSIA_DIR}/.config" ]]; then
        source "${FUCHSIA_DIR}/.config" || return 1

        # After transition period, this branch is removed, and only the else
        # branch remains. Transition period to end April 2019.
        fx-build-dir-write "${FUCHSIA_BUILD_DIR}" || return 1
        # We must remove the old default config or it has "sticky" behavior.
        rm -f -- "${FUCHSIA_DIR}/.config"
      else
        return 1
      fi

    fi

    # .fx-build-dir contains $FUCHSIA_BUILD_DIR
    FUCHSIA_BUILD_DIR="$(<"${FUCHSIA_DIR}/.fx-build-dir")"
    if [[ -z "${FUCHSIA_BUILD_DIR}" ]]; then
      return 1
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

  if ! fx-build-config-load; then
    exit $?
  fi
}

function fx-build-dir-write {
  local build_dir="$1"

  local -r tempfile="$(mktemp)"
  echo "${build_dir}" > "${tempfile}"
  mv -f "${tempfile}" "${FUCHSIA_DIR}/.fx-build-dir"
}

function get-device-name {
  fx-config-read
  # If DEVICE_NAME was passed in fx -d, use it
  if [[ "${FUCHSIA_DEVICE_NAME+isset}" == "isset" ]]; then
    echo "${FUCHSIA_DEVICE_NAME}"
    return
  fi
  # Uses a file outside the build dir so that it is not removed by `gn clean`
  local pairfile="${FUCHSIA_BUILD_DIR}.device"
  # If .device file exists, use that
  if [[ -f "${pairfile}" ]]; then
    echo "$(<"${pairfile}")"
    return
  fi
  echo ""
}

function get-fuchsia-device-addr {
  fx-command-run netaddr "$(get-device-name)" --fuchsia "$@"
}

function get-netsvc-device-addr {
  fx-command-run netaddr "$(get-device-name)" "$@"
}

# if $1 is "-d" or "--device" return
#   - the netaddr if $2 looks like foo-bar-baz-flarg
#     OR
#   - $2 if it doesn't
# else return ""
# if -z is supplied as the third argument, get the netsvc
# address instead of the netstack one
function get-device-addr {
  device=""
  if [[ "$1" == "-d" || "$1" == "--device" ]]; then
    shift
    if [[ "$1" == *"-"* ]]; then
      if [[ "$2" != "-z" ]]; then
        device="$(fx-command-run netaddr "$1" --fuchsia)"
      else
        device="$(fx-command-run netaddr "$1")"
      fi
    else
      device="$1"
    fi
    shift
  fi
  echo "${device}"
}

function fx-command-run {
  local -r command_name="$1"
  local -r command_path="${FUCHSIA_DIR}/tools/devshell/${command_name}"

  if [[ ! -f "${command_path}" ]]; then
    fx-error "Unknown command ${command_name}"
    exit 1
  fi

  shift
  "${command_path}" "$@"
}

buildtools_whitelist=" gn ninja "

function fx-buildtool-run {
  local -r command_name="$1"
  local -r command_path="${FUCHSIA_DIR}/buildtools/${command_name}"

  if [[ ! "${buildtools_whitelist}" =~ .*[[:space:]]"${command_name}"[[:space:]].* ]]; then
    fx-error "command ${command_name} not allowed"
    exit 1
  fi

  if [[ ! -f "${command_path}" ]]; then
    fx-error "Unknown command ${command_name}"
    exit 1
  fi

  shift
  "${command_path}" "$@"
}

function fx-command-exec {
  local -r command_name="$1"
  local -r command_path="${FUCHSIA_DIR}/tools/devshell/${command_name}"

  if [[ ! -f "${command_path}" ]]; then
    fx-error "Unknown command ${command_name}"
    exit 1
  fi

  shift
  exec "${command_path}" "$@"
}

function fx-print-command-help {
  local -r command_path="$1"
  if grep '^## ' "$command_path" > /dev/null; then
    sed -n -e 's/^## //p' -e 's/^##$//p' < "$command_path"
  else
    local -r command_name=$(basename "$command_path")
    echo "No help found. Try \`fx $command_name -h\`"
  fi
}

function fx-command-help {
  fx-print-command-help "$0"
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
  if grep -q "use_goma = true" "${FUCHSIA_BUILD_DIR}/args.gn"; then
    # The recommendation from the Goma team is to use 10*cpu-count.
    local cpus="$(fx-cpu-count)"
    echo $((cpus * 10))
  else
    fx-cpu-count
  fi
}

function fx-cpu-count {
  local -r cpu_count=$(getconf _NPROCESSORS_ONLN)
  echo "$cpu_count"
}

# Define the global lock file in a readonly variable, setting its value if it
# was never defined before. Need to do it in two steps since "readonly
# FX_LOCK_FILE=..." does not work because we include this file multiple times in
# one `fx` run.
: ${FX_LOCK_FILE:="${FUCHSIA_DIR}/.build_lock"}
readonly FX_LOCK_FILE

# Use a lock file around a command if possible.
# Print a message if the lock isn't immediately entered,
# and block until it is.
function fx-try-locked {
  if ! command -v shlock >/dev/null; then
    # Can't lock! Fall back to unlocked operation.
    fx-exit-on-failure "$@"
  elif shlock -f "${FX_LOCK_FILE}" -p $$; then
    # This will cause a deadlock if any subcommand calls back to fx build,
    # because shlock isn't reentrant by forked processes.
    fx-cmd-locked "$@"
  else
    echo "Locked by ${FX_LOCK_FILE}..."
    while ! shlock -f "${FX_LOCK_FILE}" -p $$; do sleep .1; done
    fx-cmd-locked "$@"
  fi
}

function fx-cmd-locked {
  # Exit trap to clean up lock file
  trap "[[ -n \"${FX_LOCK_FILE}\" ]] && rm -f \"${FX_LOCK_FILE}\"" EXIT
  fx-exit-on-failure "$@"
}

function fx-exit-on-failure {
  "$@" || exit $?
}

# Massage a ninja command line to add default -j and/or -l switches.
# Note this takes the ninja command itself as first argument.
# This can be used both to run ninja directly or to run a wrapper script.
function fx-run-ninja {
  # Separate the command from the arguments so we can prepend default -j/-l
  # switch arguments.  They need to come before the user's arguments in case
  # those include -- or something else that makes following arguments not be
  # handled as normal switches.
  local cmd="$1"
  shift

  local args=()
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

  if ! $have_load; then
    if [[ "$(uname -s)" == "Darwin" ]]; then
      # Load level on Darwin is quite different from that of Linux, wherein a
      # load level of 1 per CPU is not necessarily a prohibitive load level. An
      # unscientific study of build side effects suggests that cpus*20 is a
      # reasonable value to prevent catastrophic load (i.e. user can not kill
      # the build, can not lock the screen, etc).
      local cpus="$(fx-cpu-count)"
      args=("-l" $((cpus * 20)) "${args[@]}")
    fi
  fi

  if ! $have_jobs; then
    local concurrency="$(fx-choose-build-concurrency)"
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

  # TERM is passed for the pretty ninja UI
  # PATH is passed as some tools are referenced via $PATH due to platform differences.
  # TMPDIR is passed for Goma on macOS. TMPDIR must be set, or unset, not
  # empty. Some Dart build tools have been observed writing into source paths
  # when TMPDIR="" - it is deliberately unquoted and using the ${+} expansion
  # expression).
  fx-try-locked env -i TERM="${TERM}" PATH="${PATH}" \
    ${NINJA_STATUS+"NINJA_STATUS=${NINJA_STATUS}"} \
    ${TMPDIR+"TMPDIR=$TMPDIR"} \
    "$cmd" "${args[@]}"
}
