# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

#
# Source this script into BASH or ZSH to include various useful functions
# in your environment.
#
#   $ source path/to/fuchsia/scripts/env.sh
#   $ envprompt
#   $ fgo
#   $ fset x86-64
#   $ fbuild
#
# Functions prefixed with 'f' are for fuchsia.
# Functions prefixed with 'm' are for magenta.
#

if [[ -n "${ZSH_VERSION}" ]]; then
  export FUCHSIA_SCRIPTS_DIR=${${(%):-%x}:a:h}
else
  export FUCHSIA_SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fi

case "$(uname -s)" in
  Darwin)
    export HOST_PLATFORM="mac-x64"
    ;;
  Linux)
    export HOST_PLATFORM="linux-x64"
    ;;
esac

export FUCHSIA_DIR="$(dirname "${FUCHSIA_SCRIPTS_DIR}")"
export FUCHSIA_OUT_DIR="${FUCHSIA_DIR}/out"
export MAGENTA_DIR="${FUCHSIA_DIR}/magenta"
export QEMU_DIR="${FUCHSIA_DIR}/buildtools/${HOST_PLATFORM}/qemu/bin"
export FUCHSIA_ENV_SH_VERSION="$(git --git-dir=${FUCHSIA_SCRIPTS_DIR}/.git rev-parse HEAD)"

function __netaddr() {
  # We want to give the user time to accept Darwin's firewall dialog.
  if [[ "$(uname -s)" = "Darwin" ]]; then
    netaddr "$@"
  else
    netaddr --nowait "$@"
  fi
}
### envhelp: print usage for command or list of commands

function env_sh_version() {
  echo $FUCHSIA_ENV_SH_VERSION
}

function envhelp() {
  if [[ $# -ne 1 ]]; then
    # note: please keep these sorted
    cat <<END
env.sh functions:
  envhelp, env_sh_version
magenta functions:
  mboot, mbuild, mcheck, mgo, mrun, mset, msymbolize
fuchsia functions:
  fboot, fbuild, fbuild-sysroot, fcheck, fcmd, fcp, fgen,
  fgen-if-changed, fgo, finstall, freboot, frun, fset, fsymbolize, ftrace
END
    return
  fi
  $1-usage
}

### envprompt: embed information about the build environment in the shell prompt

function envprompt-info() {
  if ! [[ -z "${ENVPROMPT_INFO}" ]]; then
    echo "[${ENVPROMPT_INFO}] "
  fi
}

function envprompt() {
  if [[ -n "${ZSH_VERSION}" ]]; then
    autoload -Uz colors && colors
    setopt PROMPT_SUBST
    export PS1='%B%F{yellow}$(envprompt-info)%B%F{blue}%m:%~%#%f%b '
    export PS2='%B%F{blue}>%f%b '
  else
    export PS1='\[\e[0;1;33m\]$(envprompt-info)\[\e[34m\]\h:\w\\$\[\e[0m\] '
    export PS2='\[\e[0;1;34m\]>\[\e[0m\] '
  fi
}

### mgo: navigate to directory within magenta

function mgo-usage() {
  cat >&2 <<END
Usage: mgo [dir]
Navigates to directory within magenta.
END
}

function mgo() {
  if [[ $# -gt 1 ]]; then
    mgo-usage
    return 1
  fi

  cd "${MAGENTA_DIR}/$1"
}

if [[ -z "${ZSH_VERSION}" ]]; then
  function _mgo() {
    local cur
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    COMPREPLY=($(ls -dp1 --color=never ${MAGENTA_DIR}/${cur}* 2>/dev/null | \
      sed -n "s|^${MAGENTA_DIR}/\(.*/\)\$|\1|p" | xargs echo))
  }
  complete -o nospace -F _mgo mgo
fi

### mset: set magenta build properties

function mset-usage() {
  cat >&2 <<END
Usage: mset x86-64|arm64|rpi3|odroidc2|hikey960
Sets magenta build options.
END
}

function mset() {
  if [[ $# -ne 1 ]]; then
    mset-usage
    return 1
  fi

  local settings="$*"

  case $1 in
    x86-64)
      export MAGENTA_PROJECT=magenta-pc-x86-64
      export MAGENTA_ARCH=x86-64
      export MAGENTA_BUILD_TARGET=x86_64
      ;;
    arm64)
      export MAGENTA_PROJECT=magenta-qemu-arm64
      export MAGENTA_ARCH=arm64
      export MAGENTA_BUILD_TARGET=aarch64
      ;;
    rpi3)
      export MAGENTA_PROJECT=magenta-rpi3-arm64
      export MAGENTA_ARCH=arm64
      export MAGENTA_BUILD_TARGET=rpi3
      ;;
    odroidc2)
      export MAGENTA_PROJECT=magenta-odroidc2-arm64
      export MAGENTA_ARCH=arm64
      export MAGENTA_BUILD_TARGET=odroidc2
      ;;
    hikey960)
      export MAGENTA_PROJECT=magenta-hikey960-arm64
      export MAGENTA_ARCH=arm64
      export MAGENTA_BUILD_TARGET=hikey960
      ;;
    *)
      mset-usage
      return 1
  esac

  export MAGENTA_BUILD_ROOT="${FUCHSIA_OUT_DIR}/build-magenta"
  export MAGENTA_BUILD_DIR="${MAGENTA_BUILD_ROOT}/build-${MAGENTA_PROJECT}"
  export MAGENTA_TOOLS_DIR="${MAGENTA_BUILD_ROOT}/tools"
  export MAGENTA_BUILD_REV_CACHE="${MAGENTA_BUILD_DIR}/build.rev";
  export MAGENTA_SETTINGS="${settings}"
  export ENVPROMPT_INFO="${MAGENTA_ARCH}"

  # add tools to path, removing prior tools directory if any
  export PATH="${PATH//:${MAGENTA_DIR}\/build-*\/tools}:${MAGENTA_TOOLS_DIR}"
}

### mcheck: checks whether mset was run

function mcheck-usage() {
  cat >&2 <<END
Usage: mcheck
Checks whether magenta build options have been set.
END
}

function mcheck() {
  if [[ -z "${MAGENTA_SETTINGS}" ]]; then
    echo "Must run mset first (see envhelp for more)." >&2
    return 1
  fi
  return 0
}

### mbuild: build magenta

function mbuild-usage() {
  cat >&2 <<END
Usage: mbuild [extra make args...]
Builds magenta.
END
}

function mbuild() {
  mcheck || return 1

  echo "Building magenta..." \
    && "${FUCHSIA_SCRIPTS_DIR}/build-magenta.sh" \
         -t "${MAGENTA_BUILD_TARGET}" "$@"
}

function mbuild-if-changed() {
  echo "Deprecated - just run mbuild"
  mbuild
}

### mboot: run magenta bootserver

function mboot-usage() {
  cat >&2 <<END
Usage: mboot [extra bootserver args...]
Runs magenta system bootserver.
END
}

function mboot() {
  mcheck || return 1

  "${MAGENTA_TOOLS_DIR}/bootserver" "${MAGENTA_BUILD_DIR}/magenta.bin" "$@"
}

### mrun: run magenta in qemu

function mrun-usage() {
  cat >&2 <<END
Usage: mrun [extra qemu args...]
Runs magenta system in qemu.
END
}

function mrun() {
  mcheck || return 1

  "${MAGENTA_DIR}/scripts/run-magenta" -o "${MAGENTA_BUILD_DIR}" -a "${MAGENTA_ARCH}" \
    -q "${QEMU_DIR}" $@
}

### msymbolize: symbolizes stack traces

function msymbolize-usage() {
  cat >&2 <<END
Usage: msymbolize [extra symbolize args...]
Symbolizes stack trace from standard input.
END
}

function msymbolize() {
  mcheck || return 1

  # TODO(jeffbrown): Fix symbolize to support arch other than x86-64
  "${MAGENTA_DIR}/scripts/symbolize" --build-dir "${MAGENTA_BUILD_DIR}" "$@"
}

### mlog: run magenta log listener

function mlog-usage() {
  cat >&2 <<END
Usage: mlog [extra log listener args...]
Runs magenta log listener.
END
}

function mlog() {
  mcheck || return 1

  "${MAGENTA_TOOLS_DIR}/loglistener" "$@"
}


### fgo: navigate to directory within fuchsia

function fgo-usage() {
  cat >&2 <<END
Usage: fgo [dir]
Navigates to directory within fuchsia root.
END
}

function fgo() {
  if [[ $# -gt 1 ]]; then
    fgo-usage
    return 1
  fi

  cd "${FUCHSIA_DIR}/$1"
}

if [[ -z "${ZSH_VERSION}" ]]; then
  function _fgo() {
    local cur
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    COMPREPLY=($(/bin/ls -dp1 ${FUCHSIA_DIR}/${cur}* 2>/dev/null | \
      sed -n "s|^${FUCHSIA_DIR}/\(.*/\)\$|\1|p" | xargs echo))
  }
  complete -o nospace -F _fgo fgo
fi

### fset: set fuchsia build properties

function fset-usage() {
  # Note: if updating the syntax here please update zsh-completion/_fset to match
  cat >&2 <<END
Usage: fset x86-64|arm64|rpi3|odroidc2|hikey960
                              [--release] [--modules m1,m2...]
                              [--goma|--no-goma] [--no-ensure-goma]
                              [--goma-dir path]
                              [--ccache|--no-ccache]
                              [--dart-analysis]
Sets fuchsia build options.
END
}

function fset-add-gen-arg() {
  export FUCHSIA_GEN_ARGS="${FUCHSIA_GEN_ARGS} $*"
}

function fset-add-ninja-arg() {
  export FUCHSIA_NINJA_ARGS="${FUCHSIA_NINJA_ARGS} $*"
}

function fset() {
  if [[ $# -lt 1 ]]; then
    fset-usage
    return 1
  fi

  local settings="$*"
  export FUCHSIA_GEN_ARGS=
  export FUCHSIA_NINJA_ARGS=
  export FUCHSIA_VARIANT=debug

  case $1 in
    x86-64)
      mset x86-64
      # TODO(jeffbrown): we should really align these
      export FUCHSIA_GEN_TARGET=x86-64
      ;;
    arm64|rpi3|odroidc2|hikey960)
      mset $1
      export FUCHSIA_GEN_TARGET=aarch64
      ;;
    *)
      fset-usage
      return 1
  esac
  fset-add-gen-arg --target_cpu "${FUCHSIA_GEN_TARGET}"
  shift

  local goma
  local goma_dir
  local ensure_goma=1
  local ccache
  local dart_analysis=0
  while [[ $# -ne 0 ]]; do
    case $1 in
      --release)
        fset-add-gen-arg --release
        export FUCHSIA_VARIANT=release
        ;;
      --modules)
        if [[ $# -lt 2 ]]; then
          fset-usage
          return 1
        fi
        fset-add-gen-arg --modules $2
        shift
        ;;
      --goma)
        goma=1
        ;;
      --no-goma)
        goma=0
        ;;
      --no-ensure-goma)
        ensure_goma=0
        ;;
      --goma-dir)
        if [[ $# -lt 2 ]]; then
          fset-usage
          return 1
        fi
        goma_dir=$2
        if [[ ! -d "${goma_dir}" ]]; then
          echo -e "GOMA directory does not exist: "${goma_dir}""
          return 1
        fi
        shift
        ;;
      --ccache)
        ccache=1
        ;;
      --no-ccache)
        ccache=0
        ;;
      --dart-analysis)
        dart_analysis=1
        ;;
      *)
        fset-usage
        return 1
    esac
    shift
  done

  export FUCHSIA_BUILD_DIR="${FUCHSIA_OUT_DIR}/${FUCHSIA_VARIANT}-${FUCHSIA_GEN_TARGET}"
  export FUCHSIA_BUILD_NINJA="${FUCHSIA_BUILD_DIR}/build.ninja"
  export FUCHSIA_GEN_ARGS_CACHE="${FUCHSIA_BUILD_DIR}/build.gen-args"
  export FUCHSIA_SETTINGS="${settings}"
  export FUCHSIA_ENSURE_GOMA="${ensure_goma}"
  export GOPATH="${FUCHSIA_BUILD_DIR}"

  # If a goma directory wasn't specified explicitly then default to "~/goma".
  if [[ -n "${goma_dir}" ]]; then
    export FUCHSIA_GOMA_DIR="${goma_dir}"
  else
    export FUCHSIA_GOMA_DIR=~/goma
  fi

  fset-add-ninja-arg -C "${FUCHSIA_BUILD_DIR}"

  # Automatically detect goma and ccache if not specified explicitly.
  if [[ -z "${goma}" ]] && [[ -z "${ccache}" ]]; then
    if [[ -d "${FUCHSIA_GOMA_DIR}" ]]; then
      goma=1
    elif [[ -n "${CCACHE_DIR}" ]] && [[ -d "${CCACHE_DIR}" ]]; then
      ccache=1
    fi
  fi

  # Add --goma or --ccache as appropriate.
  local builder=
  if [[ "${goma}" -eq 1 ]]; then
    fset-add-gen-arg --goma "${FUCHSIA_GOMA_DIR}"
    # macOS needs a lower value of -j parameter, because it has a limit on the
    # number of open file descriptors. Use 4 * cpu_count, which works well in
    # practice.
    if [[ "$(uname -s)" = "Darwin" ]]; then
      numjobs=$(( $(sysctl -n hw.ncpu) * 4 ))
      fset-add-ninja-arg -j ${numjobs}
    else
      fset-add-ninja-arg -j 1000
    fi
    builder="-goma"
  elif [[ "${ccache}" -eq 1 ]]; then
    fset-add-gen-arg --ccache
    builder="-ccache"
  fi

  if [[ "${dart_analysis}" -eq 1 ]]; then
    fset-add-gen-arg --with-dart-analysis
  fi

  export ENVPROMPT_INFO="${ENVPROMPT_INFO}-${FUCHSIA_VARIANT}${builder}"
}

### fcheck: checks whether fset was run

function fcheck-usage() {
  cat >&2 <<END
Usage: fcheck
Checks whether fuchsia build options have been set.
END
}

function fcheck() {
  if [[ -z "${FUCHSIA_SETTINGS}" ]]; then
    echo "Must run fset first (see envhelp for more)." >&2
    return 1
  fi
  return 0
}

### fgen: generate ninja build files

function fgen-usage() {
  cat >&2 <<END
Usage: fgen [extra gen.py args...]
Generates ninja build files for fuchsia.
END
}

function fgen-internal() {
  if [[ -n "${ZSH_VERSION}" ]]; then
    "${FUCHSIA_DIR}/packages/gn/gen.py" ${=FUCHSIA_GEN_ARGS} "$@"
  else
    "${FUCHSIA_DIR}/packages/gn/gen.py" ${FUCHSIA_GEN_ARGS} "$@"
  fi
}

function fgen() {
  fcheck || return 1

  echo "Generating ninja files..."
  rm -f "${FUCHSIA_GEN_ARGS_CACHE}"
  mbuild \
    && fgen-internal "$@" \
    && (echo "${FUCHSIA_GEN_ARGS}" > "${FUCHSIA_GEN_ARGS_CACHE}")
}

### fgen-if-changed: only generate ninja build files if stale

function fgen-if-changed-usage() {
  cat >&2 <<END
Usage: fgen-if-changed [extra gen.py args...]
Generates ninja build files for fuchsia only if gen args have changed.
END
}

function fgen-if-changed() {
  fcheck || return 1

  local last_gen_args
  if [[ -f "${FUCHSIA_GEN_ARGS_CACHE}" ]]; then
    last_gen_args=$(cat "${FUCHSIA_GEN_ARGS_CACHE}")
  fi

  if ! ([[ -f "${FUCHSIA_BUILD_NINJA}" ]] \
      && [[ "${FUCHSIA_GEN_ARGS}" == "${last_gen_args}" ]]); then
    fgen "$@"
  fi
}

### fsysroot: build sysroot

function fbuild-sysroot() {
  echo "Deprecated - just run fbuild"
  mbuild
}

function fbuild-sysroot-if-changed() {
  echo "Deprecated - just run mbuild"
  mbuild
}

### fbuild: build fuchsia

function fbuild-usage() {
  cat >&2 <<END
Usage: fbuild [extra ninja args...]
Builds fuchsia.
END
}

function fbuild-goma-ensure-start() {
  if ([[ "${FUCHSIA_GEN_ARGS}" == *--goma* ]] \
      && [[ "${FUCHSIA_ENSURE_GOMA}" -eq 1 ]]); then
    "${FUCHSIA_GOMA_DIR}"/goma_ctl.py ensure_start
  fi
}

function fbuild-internal() {
  if [[ -n "${ZSH_VERSION}" ]]; then
    "${FUCHSIA_DIR}/buildtools/ninja" ${=FUCHSIA_NINJA_ARGS} "$@"
  else
    "${FUCHSIA_DIR}/buildtools/ninja" ${FUCHSIA_NINJA_ARGS} "$@"
  fi
}

function fbuild() {
  fcheck || return 1

  mbuild \
    && fgen-if-changed \
    && fbuild-goma-ensure-start \
    && echo "Building fuchsia..." \
    && fbuild-internal "$@"
}

function __fbuild_make_batch() {
  # parse a bootfs manifest, compare inputs to a stamp file and append
  # sftp commands to a batch file to update changed files.
  local manifest stamp batch_file userfs_path build_path
  manifest=$1
  stamp=$2
  batch_file=$3

  while IFS=\= read userfs_path build_path; do
    if [[ -z "${build_path}" ]]; then
      continue
    fi

    if [[ $build_path -nt $stamp ]]; then
      local device_path=/system/${userfs_path}
      echo "Updating ${device_path} with ${build_path}"
      echo "-rm ${device_path}" >> "$batch_file"
      echo "put ${build_path} ${device_path}" >> "$batch_file"
    fi
  done < "$manifest"
}

function fbuild-sync() {
  local stamp status_file batch_file package host

  stamp="${FUCHSIA_BUILD_DIR}/.fbuild-sync-stamp"
  status_file="${FUCHSIA_BUILD_DIR}/.fbuild-sync-status"
  batch_file="${FUCHSIA_BUILD_DIR}/.fbuild-sync-batchfile"

  touch "$status_file"
  if [[ "$(cat $status_file)" != "failed" ]]; then
    touch $stamp
  fi

  fbuild $*

  if [ $? -ne 0 ]; then
    echo failed > $status_file
    return 1
  fi

  echo -n > "$batch_file"

  __fbuild_make_batch \
    "${FUCHSIA_BUILD_DIR}/gen/packages/gn/system.bootfs.manifest" \
    "$stamp" "$batch_file"

  while read package; do
    __fbuild_make_batch \
      "${FUCHSIA_BUILD_DIR}/package/$package/system_manifest" \
      "$stamp" "$batch_file"
  done < ${FUCHSIA_BUILD_DIR}/gen/packages/gn/packages

  echo "Syncing changed system.bootfs files..."
  host="$(__netaddr --fuchsia)"
  fsftp -q -b "${batch_file}" "[${host}]" > /dev/null
  if [ $? -ne 0 ]; then
    echo failed > "${status_file}"
    return 1
  fi

  rm -f "${stamp}"
  rm -f "${status_file}"
  rm -f "${batch_file}"
}

### fsyncvol: update a persistent fuchsia system

fsyncvol() {
  $FUCHSIA_SCRIPTS_DIR/make-fuchsia-vol/sync-fuchsia-vol.sh $@
}

### fboot: run fuchsia bootserver

function fboot-usage() {
  cat >&2 <<END
Usage: fboot [extra bootserver args...]
Runs fuchsia system bootserver.
END
}

function fboot() {
  fcheck || return 1

  mboot "${FUCHSIA_BUILD_DIR}/user.bootfs" "$@"
}

### finstall: build installer image and run boot server

function finstall-usage() {
  cat >&2 <<END
Usage: finstall [extra bootserver args...]
Builds installer image and runs fuchsia system bootserver with it.
END
}

function finstall() {
  fcheck || return 1

  "${FUCHSIA_SCRIPTS_DIR}/installer/build-installable-userfs.sh" \
      -b "${FUCHSIA_BUILD_DIR}" \
    && echo "After netbooting, please run 'install-fuchsia' on the device to complete the installation." \
    && mboot "${FUCHSIA_BUILD_DIR}/installer.bootfs" "$@"
}

### frun: run fuchsia in qemu

function frun-usage() {
  cat >&2 <<END
Usage: frun [extra qemu args...]
Runs fuchsia system in qemu.
END
}

function frun() {
  fcheck || return 1

  mrun -x "${FUCHSIA_BUILD_DIR}/user.bootfs" "$@"
}

### fbox: run fuchsia in virtualbox

function fbox() {
  fcheck || return 1

  "$FUCHSIA_SCRIPTS_DIR/vbox/fbox.sh" "$@"
}

### fsymbolize: symbolizes stack traces with fuchsia build

function fsymbolize-usage() {
  cat >&2 <<END
Usage: fsymbolize [extra symbolize args...]
Symbolizes stack trace from standard input.
END
}

function fsymbolize() {
  fcheck || return 1

  # TODO(jeffbrown): Fix symbolize to support arch other than x86-64
  "${MAGENTA_DIR}/scripts/symbolize" \
      --build-dir "${MAGENTA_BUILD_DIR}" "${FUCHSIA_BUILD_DIR}" "$@"
}

### freboot: reboot the attached device

function freboot-usage() {
  cat >&2 <<END
Usage: freboot
Reboots the attached device.
END
}

function freboot() {
  if [[ $# -gt 1 ]]; then
      freboot_usage
      return 1
  fi

  # Add timeout for OS X users so they can click the network connection warning
  # dialog.
  local timeout_flag
  if [[ "$(uname -s)" = "Darwin" ]]; then
    timeout_flag="--timeout=3000"
  else
    timeout_flag="--nowait"
  fi

  freboot_host=${1:-":"}
  fcheck || return 1
  echo "Rebooting system..."
  netruncmd $timeout_flag "${freboot_host}" "dm reboot"
}

### ftrace: collects and presents traces

function ftrace-usage() {
  cat >&2 <<END
Usage: ftrace [--help] [extra trace.sh args...]
Starts a trace.
END
}

function ftrace() {
  fcheck || return 1

  "${FUCHSIA_DIR}/apps/tracing/scripts/trace.sh" "$@"
}

### fmkbootloader: builds the Fuchsia bootloader and places it on an external
### drive

function fmkbootloader-usage() {
  cat >&2 <<END
Usage: fmkbootloader <root of external drive>
Builds Fuchsia bootloader and copies it to a drive of your choice.
END
}

function fmkbootloader() {
  if [[ $# -ne 1 ]]; then
    fmkbootloader-usage
    return 1
  fi

  fcheck || return 1

  if [[ ! -d $1 ]]; then
    echo >&2 "Drive at $1 does not appear to be mounted."
    return 1
  fi

  local TARGET_DIR=$1/EFI/BOOT
  (
    set -e
    mbuild
    mkdir -p ${TARGET_DIR}
    cp ${MAGENTA_BUILD_DIR}/bootloader/bootx64.efi \
      ${TARGET_DIR}/BOOTX64.EFI
  ) && \
    echo "Bootloader loaded to $1"
}

go() {
  if [[ "$GOOS" == "fuchsia" ]]; then
    "${FUCHSIA_DIR}/buildtools/go" "$@"
  else
    # /usr/bin/which avoids cross-shell subtleties, exists on linux & osx.
    "$(/usr/bin/which go)" "$@"
  fi
}

function fssh() {
  SSH_AUTH_SOCK="" ssh -F $FUCHSIA_BUILD_DIR/ssh-keys/ssh_config $*
}

function fscp() {
  SSH_AUTH_SOCK="" scp -F $FUCHSIA_BUILD_DIR/ssh-keys/ssh_config $*
}

function fsftp() {
  SSH_AUTH_SOCK="" sftp -F $FUCHSIA_BUILD_DIR/ssh-keys/ssh_config $*
}

function fcmd() {
  local host="$(__netaddr --fuchsia)"
  fssh -q "${host}" $*
  local r=$?
  if [ $r -ne 0 ]; then
    echo "fssh exited with a non-zero status."
  fi
  return $r
}

function fcp-usage() {
  cat >&2 <<END
Usage: fcp src dst
Copies a file from the host to the target device.
END
}

function fcp() {
  if [[ $# -ne 2 ]]; then
    fcp-usage
    return 1
  fi

  local src=$1
  local dst=$2
  local host="$(__netaddr --fuchsia)"

  fsftp -q -b - "[${host}]" > /dev/null << EOF
- rm ${dst}
put ${src} ${dst}
EOF
}

function ftest-usage() {
  cat >&2 <<END
Usage: ftest target <args>
Builds the specified target (e.g., ftl_unittests), copies it to the target, and
executes it. Useful for tight iterations on unittests.
END
}

function ftest() {
  if [[ $# -eq 0 ]]; then
    ftest-usage
    return 1
  fi
  local target="$1"

  fbuild-internal "${target}"
  if [[ $? -ne 0 ]]; then
    return 1
  fi
  fcp "${FUCHSIA_BUILD_DIR}/${target}" "/tmp/${target}"
  fcmd "/tmp/${target}" "${@:1}"
}

function fclock() {
  local device_date
  if [[ "$(uname -s)" = "Darwin" ]]; then
    device_date=`date +%Y-%m-%dT%T`
  else
    device_date=`date -Iseconds`
  fi

  echo "Setting device's clock to ${device_date}"
  fcmd "clock --set ${device_date}"
}

if [[ -n "${ZSH_VERSION}" ]]; then
  ### Zsh Completion
  if [[ ${fpath[(Ie)${FUCHSIA_SCRIPTS_DIR}/zsh-completion]} -eq 0 ]]; then
    # if the fuchsia zsh completion dir isn't in the fpath yet...
    # add zsh completion function dir to the fpath
    fpath=(${FUCHSIA_SCRIPTS_DIR}/zsh-completion $fpath[@])
    # load and run compinit
    autoload -U compinit
    compinit
  fi
fi

alias gce="$FUCHSIA_SCRIPTS_DIR/gce/gce"
