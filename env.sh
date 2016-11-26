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
  export FUCHSIA_SCRIPTS_DIR="$(cd "$(dirname "${(%):-%x}")" && pwd)"
else
  export FUCHSIA_SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fi
export FUCHSIA_DIR="$(dirname "${FUCHSIA_SCRIPTS_DIR}")"
export FUCHSIA_OUT_DIR="${FUCHSIA_DIR}/out"
export MAGENTA_DIR="${FUCHSIA_DIR}/magenta"

### envhelp: print usage for command or list of commands

function envhelp() {
  if [[ $# -ne 1 ]]; then
    # note: please keep these sorted
    cat <<END
magenta functions:
  mboot, mbuild, mbuild-if-changed, mcheck, mgo, mrev, mrun, mset, msymbolize
fuchsia functions:
  fboot, fbuild, fbuild-sysroot, fbuild-sysroot-if-changed, fcheck, fgen,
  fgen-if-changed, fgo, freboot, frun, fset, fsymbolize, ftrace
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
    COMPREPLY=($(ls -dp1 ${MAGENTA_DIR}/${cur}* 2>/dev/null | \
      sed -n "s|^${MAGENTA_DIR}/\(.*/\)\$|\1|p" | xargs echo))
  }
  complete -o nospace -F _mgo mgo
fi

### mset: set magenta build properties

function mset-usage() {
  cat >&2 <<END
Usage: mset x86-64|arm32|arm64
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
      ;;
    arm32)
      export MAGENTA_PROJECT=magenta-qemu-arm32
      export MAGENTA_ARCH=arm32
      ;;
    arm64)
      export MAGENTA_PROJECT=magenta-qemu-arm64
      export MAGENTA_ARCH=arm64
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
    && "${MAGENTA_DIR}/scripts/make-parallel" -C "${MAGENTA_DIR}" \
         "BUILDROOT=${MAGENTA_BUILD_ROOT}" "${MAGENTA_PROJECT}" "$@" \
    && (mrev > "${MAGENTA_BUILD_REV_CACHE}")
}

### mbuild-if-changed: only build magenta if stale

function mbuild-if-changed-usage() {
  cat >&2 <<END
Usage: mbuild-if-changed [extra make args...]
Builds magenta only if HEAD revision has changed.
END
}

function mbuild-if-changed() {
  mcheck || return 1

  local last_rev
  if [[ -f "${MAGENTA_BUILD_REV_CACHE}" ]]; then
    last_rev=$(cat "${MAGENTA_BUILD_REV_CACHE}")
  fi

  if ! ([[ -d "${MAGENTA_BUILD_DIR}" ]] \
      && [[ "$(mrev)" == "${last_rev}" ]]); then
    mbuild "$@"
  fi
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

  "${MAGENTA_DIR}/scripts/run-magenta" -o "${MAGENTA_BUILD_DIR}" -a "${MAGENTA_ARCH}" "$@"
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
  "${MAGENTA_DIR}/scripts/symbolize" "$@"
}

### mrev: prints magenta HEAD revision

function mrev-usage() {
  cat >&2 <<END
Usage: mrev
Prints magenta HEAD revision from git work tree.
END
}

function mrev() {
  if [[ $# -ne 0 ]]; then
    mrev-usage
    return 1
  fi

  (mgo && git rev-parse --verify HEAD)
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
    COMPREPLY=($(ls -dp1 ${FUCHSIA_DIR}/${cur}* 2>/dev/null | \
      sed -n "s|^${FUCHSIA_DIR}/\(.*/\)\$|\1|p" | xargs echo))
  }
  complete -o nospace -F _fgo fgo
fi

### fset: set fuchsia build properties

function fset-usage() {
  cat >&2 <<END
Usage: fset x86-64|arm64 [--release] [--modules m1,m2...]
                         [--goma|--no-goma] [--ccache|--no-ccache]
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
      export FUCHSIA_SYSROOT_TARGET=x86_64
      ;;
    arm64)
      mset arm64
      export FUCHSIA_GEN_TARGET=aarch64
      export FUCHSIA_SYSROOT_TARGET=aarch64
      ;;
    *)
      fset-usage
      return 1
  esac
  fset-add-gen-arg --target_cpu "${FUCHSIA_GEN_TARGET}"
  shift

  local goma
  local ccache
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
      --ccache)
        ccache=1
        ;;
      --no-ccache)
        ccache=0
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
  export FUCHSIA_SYSROOT_DIR="${FUCHSIA_OUT_DIR}/sysroot/${FUCHSIA_SYSROOT_TARGET}-fuchsia"
  export FUCHSIA_SYSROOT_REV_CACHE="${FUCHSIA_SYSROOT_DIR}/build.rev"
  export FUCHSIA_SETTINGS="${settings}"

  fset-add-ninja-arg -C "${FUCHSIA_BUILD_DIR}"

  # Automatically detect goma and ccache if not specified explicitly.
  if [[ -z "${goma}" ]] && [[ -z "${ccache}" ]]; then
    if [[ -d ~/goma ]]; then
      goma=1
    elif [[ -n "${CCACHE_DIR}" ]] && [[ -d "${CCACHE_DIR}" ]]; then
      ccache=1
    fi
  fi

  # Add --goma or --ccache as appropriate.
  local builder=
  if [[ "${goma}" -eq 1 ]]; then
    fset-add-gen-arg --goma
    # macOS needs a lower value of -j parameter, because it has a limit on the
    # number of open file descriptors. Use 15 * cpu_count, which works well in
    # practice.
    if [[ "$(uname -s)" = "Darwin" ]]; then
      numjobs=$(( $(sysctl -n hw.ncpu) * 15 ))
      fset-add-ninja-arg -j ${numjobs}
    else
      fset-add-ninja-arg -j 1000
    fi
    builder="-goma"
  elif [[ "${ccache}" -eq 1 ]]; then
    fset-add-gen-arg --ccache
    builder="-ccache"
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
  fbuild-sysroot-if-changed \
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

function fbuild-sysroot-usage() {
  cat >&2 <<END
Usage: fbuild-sysroot [extra build-sysroot.sh args...]
Builds fuchsia system root.
END
}

function fbuild-sysroot() {
  fcheck || return 1

  echo "Building sysroot..." \
    && (fgo && "./scripts/build-sysroot.sh" -t "${FUCHSIA_SYSROOT_TARGET}" "$@") \
    && (mrev > "${FUCHSIA_SYSROOT_REV_CACHE}")
}

### fbuild-sysroot-if-changed: only build sysroot if stale

function fbuild-sysroot-if-changed-usage() {
  cat >&2 <<END
Usage: fbuild-sysroot-if-changed [extra build-sysroot.sh args...]
Builds fuchsia system root only if magenta HEAD revision has changed.
END
}

function fbuild-sysroot-if-changed() {
  fcheck || return 1

  local last_rev
  if [[ -f "${FUCHSIA_SYSROOT_REV_CACHE}" ]]; then
    last_rev=$(cat "${FUCHSIA_SYSROOT_REV_CACHE}")
  fi

  if ! ([[ -d "${FUCHSIA_SYSROOT_DIR}" ]] \
      && [[ "$(mrev)" == "${last_rev}" ]]); then
    fbuild-sysroot "$@"
  fi
}

### fbuild: build fuchsia

function fbuild-usage() {
  cat >&2 <<END
Usage: fbuild [extra ninja args...]
Builds fuchsia.
END
}

function fbuild-goma-ensure-start() {
  if [[ "${FUCHSIA_GEN_ARGS}" == *--goma* ]]; then
    ~/goma/goma_ctl.py ensure_start
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

  fbuild-sysroot-if-changed \
    && fgen-if-changed \
    && fbuild-goma-ensure-start \
    && echo "Building fuchsia..." \
    && fbuild-internal "$@"
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

### fsymbolize: symbolizes stack traces with fuchsia build

function fsymbolize-usage() {
  cat >&2 <<END
Usage: fsymbolize [extra symbolize args...]
Symbolizes stack trace from standard input.
END
}

function fsymbolize() {
  fcheck || return 1

  msymbolize --build-dir "${FUCHSIA_BUILD_DIR}" "$@"
}

### freboot: reboot the attached device

function freboot-usage() {
  cat >&2 <<END
Usage: freboot
Reboots the attached device.
END
}

function freboot() {
  if [[ $# -ne 0 ]]; then
    freboot-usage
    return 1
  fi

  fcheck || return 1
  echo "Rebooting system..."
  netruncmd : "dm reboot"
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
