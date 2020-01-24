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
source "${devshell_lib_dir}/prebuilt.sh"
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
  local idstxt=()
  if [[ $# -gt 0 ]]; then
    idstxt=(-ids-rel -ids "$1")
  fi
  local symbolize="${HOST_OUT_DIR}/symbolize"
  local llvm_symbolizer="${PREBUILT_CLANG_DIR}/bin/llvm-symbolizer"
  local toolchain_dir="${PREBUILT_CLANG_DIR}/lib/debug/.build-id"
  local prebuilt_build_ids_dir="${FUCHSIA_BUILD_DIR}/gen/build/gn/prebuilt_build_ids"
  local out_dir="${FUCHSIA_BUILD_DIR}/.build-id"
  local zircon_dir="${ZIRCON_BUILDROOT}/.build-id"
  set -x
  "$symbolize" -llvm-symbolizer "$llvm_symbolizer" \
    "${idstxt[@]}" \
    -build-id-dir "$prebuilt_build_ids_dir" -build-id-dir "$toolchain_dir" \
    -build-id-dir "$out_dir" -build-id-dir "$zircon_dir"
}

function fx-gn {
  "${PREBUILT_GN}" "$@"
}

function fx-gen {
    (
      set -ex
      cd "${FUCHSIA_DIR}"
      fx-gn gen --check --export-compile-commands=default "${FUCHSIA_BUILD_DIR}"
    ) || return 1
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

  export ZIRCON_BUILDROOT="${FUCHSIA_BUILD_DIR%/}.zircon"
  export ZIRCON_TOOLS_DIR="${ZIRCON_BUILDROOT}/tools"

  # TODO(mangini): revisit once fxb/38436 is fixed.
  if [[ "${HOST_OUT_DIR:0:1}" != "/" ]]; then
    HOST_OUT_DIR="${FUCHSIA_BUILD_DIR}/${HOST_OUT_DIR}"
  fi

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
  fx-config-read
  local -r device="$(get-device-name)"
  local -r finder="${FUCHSIA_BUILD_DIR}/host-tools/device-finder"
  if [[ ! -f "${finder}" ]]; then
    fx-error "Device finder binary not found."
    fx-error "Run \"fx build\" to build host tools."
    exit 1
  fi
  local output devices
  case "$device" in
    "")
        output="$("${finder}" list --netboot --ipv4=false --mdns=false "$@")" || {
          code=$?
          fx-error "Device discovery failed with status: $code"
          exit $code
        }
        if [[ "$(echo "${output}" | wc -l)" -gt "1" ]]; then
          fx-error "Multiple devices found."
          fx-error "Please specify one of the following (check network if this fails):"
          devices="$("${finder}" list --netboot --ipv4=false --mdns=false --full)" || {
            code=$?
            fx-error "Device discovery failed with status: $code"
            exit $code
          }
          while IFS="" read -r line; do
            fx-error "\t${line}"
          done < <(printf '%s\n' "${devices}")
          exit 1
        fi
        echo "${output}" ;;
     *) "${finder}" resolve --netboot --ipv4=false --mdns=false "$@" "$device" ;;
  esac
}

function fx-find-command {
  local -r cmd=$1

  local command_path="${FUCHSIA_DIR}/tools/devshell/${cmd}"
  if [[ -x "${command_path}" ]]; then
    echo "${command_path}"
    return 0
  fi

  local command_path="${FUCHSIA_DIR}/tools/devshell/contrib/${cmd}"
  if [[ -x "${command_path}" ]]; then
    echo "${command_path}"
    return 0
  fi

  return 1
}

function fx-command-run {
  local -r command_name="$1"
  local -r command_path="$(fx-find-command ${command_name})"

  if [[ ${command_path} == "" ]]; then
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
  # Exit trap to clean up lock file
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
#                     ninja directly or to run a wrapper script.
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
  # TMPDIR is passed for Goma on macOS.
  # NINJA_STATUS is passed to control Ninja progress status.
  # GOMA_DISABLED is passed to forcefully disabling Goma.
  #
  # GOMA_DISABLED and TMPDIR must be set, or unset, not empty. Some Dart
  # build tools have been observed writing into source paths
  # when TMPDIR="" - it is deliberately unquoted and using the ${+} expansion
  # expression). GOMA_DISABLED will forcefully disable Goma even if it's set to
  # empty.
  full_cmdline=(env -i "TERM=${TERM}" "PATH=${PATH}" \
    ${NINJA_STATUS+"NINJA_STATUS=${NINJA_STATUS}"} \
    ${GOMA_DISABLED+"GOMA_DISABLED=$GOMA_DISABLED"} \
    ${TMPDIR+"TMPDIR=$TMPDIR"} \
    "$cmd" "${args[@]}")

  if [[ "${print_full_cmd}" = true ]]; then
    echo "${full_cmdline[@]}"
    echo
  fi
  fx-try-locked "${full_cmdline[@]}"
}

function fx-zbi {
  "${ZIRCON_TOOLS_DIR}/zbi" --compressed="$FUCHSIA_ZBI_COMPRESSION" "$@"
}
