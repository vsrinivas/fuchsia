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
# Functions prefixed with 'z' are for zircon.
#

if [[ -n "${ZSH_VERSION}" ]]; then
  export FUCHSIA_SCRIPTS_DIR=${${(%):-%x}:a:h}
else
  export FUCHSIA_SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fi

export FUCHSIA_DIR="$(dirname "${FUCHSIA_SCRIPTS_DIR}")"
export FUCHSIA_OUT_DIR="${FUCHSIA_DIR}/out"

FUCHSIA_ENV_SH_VERSION="$(git --git-dir=${FUCHSIA_SCRIPTS_DIR}/.git rev-parse HEAD)"

source "${FUCHSIA_DIR}/scripts/devshell/env.sh"

function __netaddr() {
  # We want to give the user time to accept Darwin's firewall dialog.
  if [[ "$(uname -s)" = "Darwin" ]]; then
    "${FUCHSIA_OUT_DIR}/build-zircon/tools/netaddr" "$@"
  else
    "${FUCHSIA_OUT_DIR}/build-zircon/tools/netaddr" --nowait "$@"
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
zircon functions:
  zboot, zbuild, zcheck, zgo, zrun, zset, zsymbolize
fuchsia functions:
  fboot, fbuild, fbuild-sysroot, fcheck, fcmd, fcp, fgo, finstall, freboot,
  frun, fset, fsymbolize
END
    return
  fi
  $1-usage
}

### envprompt: embed information about the build environment in the shell prompt

function envprompt-info() {
  if [[ -n "${ENVPROMPT_INFO}" ]]; then
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

### zgo: navigate to directory within zircon

function zgo-usage() {
  cat >&2 <<END
Usage: zgo [dir]
Navigates to directory within zircon.
END
}

function zgo() {
  if [[ $# -gt 1 ]]; then
    zgo-usage
    return 1
  fi

  cd "${FUCHSIA_DIR}/zircon/$1"
}

if [[ -z "${ZSH_VERSION}" ]]; then
  function _zgo() {
    local cur
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    COMPREPLY=($(ls -dp1 --color=never ${FUCHSIA_DIR}/zircon/${cur}* 2>/dev/null | \
      sed -n "s|^${FUCHSIA_DIR}/zircon/\(.*/\)\$|\1|p" | xargs echo))
  }
  complete -o nospace -F _zgo zgo
fi

### zcheck: checks whether zset was run

function zcheck-usage() {
  cat >&2 <<END
Usage: zcheck
Checks whether zircon build options have been set.
END
}

function zcheck() {
  if [[ -z "${ZIRCON_SETTINGS}" ]]; then
    echo "Must run zset first (see envhelp for more)." >&2
    return 1
  fi
  return 0
}

### zbuild: build zircon

function zbuild-usage() {
  cat >&2 <<END
Usage: zbuild [extra make args...]
Builds zircon.
END
}

function zbuild() {
  zcheck || return 1

  echo "Building zircon..." \
    && "${FUCHSIA_SCRIPTS_DIR}/build-zircon.sh" \
         -t "${ZIRCON_BUILD_TARGET}" "$@"
}

function zbuild-if-changed() {
  echo "Deprecated - just run zbuild"
  zbuild
}

### zboot: run zircon bootserver

function zboot-usage() {
  cat >&2 <<END
Usage: zboot [extra bootserver args...]
Runs zircon system bootserver.
END
}

function zboot() {
  zcheck || return 1

  "${FUCHSIA_OUT_DIR}/build-zircon/tools/bootserver" "${ZIRCON_BUILD_DIR}/zircon.bin" "$@"
}

### zrun: run zircon in qemu

function zrun-usage() {
  cat >&2 <<END
Usage: zrun [extra qemu args...]
Runs zircon system in qemu.
END
}

function zrun() {
  zcheck || return 1

  local qemu_dir="${QEMU_DIR:-$(source "${FUCHSIA_DIR}/buildtools/vars.sh" && echo -n ${BUILDTOOLS_QEMU_DIR})/bin}"

  "${FUCHSIA_DIR}/zircon/scripts/run-zircon" -o "${ZIRCON_BUILD_DIR}" -a "${ZIRCON_ARCH}" \
    -q "${qemu_dir}" "$@"
}

### zsymbolize: symbolizes stack traces

function zsymbolize-usage() {
  cat >&2 <<END
Usage: zsymbolize [extra symbolize args...]
Symbolizes stack trace from standard input.
END
}

function zsymbolize() {
  zcheck || return 1

  # TODO(jeffbrown): Fix symbolize to support arch other than x86-64
  "${FUCHSIA_DIR}/zircon/scripts/symbolize" --build-dir "${ZIRCON_BUILD_DIR}" "$@"
}

### zlog: run zircon log listener

function zlog-usage() {
  cat >&2 <<END
Usage: zlog [extra log listener args...]
Runs zircon log listener.
END
}

function zlog() {
  zcheck || return 1

  "${FUCHSIA_OUT_DIR}/build-zircon/tools/loglistener" "$@"
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
                              [--release] [--packages p1,p2...]
                              [--goma|--no-goma] [--no-ensure-goma]
                              [--goma-dir path] [--args gn-argument]
                              [--ccache|--no-ccache]
Sets fuchsia build options.
END
}

function fset() {
  if [[ $# -lt 1 ]]; then
    fset-usage
    return 1
  fi

  local settings="$*"
  local gen_args=
  export FUCHSIA_VARIANT=debug

  case $1 in
    x86-64)
      zset x86-64
      # TODO(jeffbrown): we should really align these
      export FUCHSIA_GEN_TARGET=x86-64
      ;;
    arm64|rpi3|odroidc2|hikey960)
      zset $1
      export FUCHSIA_GEN_TARGET=aarch64
      ;;
    *)
      fset-usage
      return 1
  esac
  gen_args="${gen_args} --target_cpu ${FUCHSIA_GEN_TARGET}"
  shift

  local use_goma
  local goma_dir
  local ensure_goma=1
  local ccache
  while [[ $# -ne 0 ]]; do
    case $1 in
      --release)
        gen_args="${gen_args} --release"
        export FUCHSIA_VARIANT=release
        ;;
      --packages)
        if [[ $# -lt 2 ]]; then
          fset-usage
          return 1
        fi
        gen_args="${gen_args} --packages $2"
        shift
        ;;
      --goma)
        use_goma=1
        ;;
      --no-goma)
        use_goma=0
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
      --args)
        if [[ $# -lt 2 ]]; then
          fset-usage
          return 1
        fi
        gen_args="${gen_args} --args $2"
        shift
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
  export FUCHSIA_SETTINGS="${settings}"

  # TODO(abarth): Remove.
  export GOPATH="${FUCHSIA_BUILD_DIR}:${FUCHSIA_DIR}/garnet/go"

  # If a goma directory wasn't specified explicitly then default to "~/goma".
  if [[ -z "${goma_dir}" ]]; then
    goma_dir="${HOME}/goma"
  fi

  # Automatically detect goma and ccache if not specified explicitly.
  if [[ -z "${use_goma}" ]] && [[ -z "${ccache}" ]]; then
    if [[ -d "${goma_dir}" ]]; then
      use_goma=1
    elif [[ -n "${CCACHE_DIR}" ]] && [[ -d "${CCACHE_DIR}" ]]; then
      ccache=1
    fi
  fi

  # Add --goma or --ccache as appropriate.
  local builder=
  if [[ "${use_goma}" -eq 1 ]]; then
    gen_args="${gen_args} --goma ${goma_dir}"
    builder="-goma"
  elif [[ "${ccache}" -eq 1 ]]; then
    gen_args="${gen_args} --ccache"
    builder="-ccache"
  fi

  export ENVPROMPT_INFO="${ENVPROMPT_INFO}-${FUCHSIA_VARIANT}${builder}"

  if [[ -n "${ZSH_VERSION}" ]]; then
    "${FUCHSIA_DIR}/packages/gn/gen.py" ${=gen_args} "$@"
  else
    "${FUCHSIA_DIR}/packages/gn/gen.py" ${gen_args} "$@"
  fi

  if [[ "${use_goma}" -eq 1 ]] && [[ "${ensure_goma}" -eq 1 ]]; then
    echo
    echo "Ensuring goma has started (skip this step by passing --no-ensure-goma)."
    "${goma_dir}/goma_ctl.py" ensure_start || return $?
  fi
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

function fgen() {
  echo "Deprecated - just run fset"
}

function fgen-if-changed() {
  echo "Deprecated - just run fset"
}

### fsysroot: build sysroot

function fbuild-sysroot() {
  echo "Deprecated - just run fbuild"
}

function fbuild-sysroot-if-changed() {
  echo "Deprecated - just run zbuild"
}

### fbuild: build fuchsia

function fbuild-usage() {
  cat >&2 <<END
Usage: fbuild [extra ninja args...]
Builds fuchsia.
END
}

function fbuild() {
  fcheck || return 1

  grep -q "use_goma = true" "${FUCHSIA_BUILD_DIR}/args.gn"
  local use_goma_result="$?"

  zbuild || return $?

  # macOS needs a lower value of -j parameter, because it has a limit on the
  # number of open file descriptors. Use 4 * cpu_count, which works well in
  # practice.
  local concurrency_args=
  if [[ use_goma_result -eq 0 ]]; then
    if [[ "$(uname -s)" = "Darwin" ]]; then
      numjobs=$(( $(sysctl -n hw.ncpu) * 4 ))
      concurrency_args="-j ${numjobs}"
    else
      concurrency_args="-j 1000"
    fi
  fi

  if [[ -n "${ZSH_VERSION}" ]]; then
    "${FUCHSIA_DIR}/buildtools/ninja" ${=concurrency_args} -C "${FUCHSIA_BUILD_DIR}" "$@"
  else
    "${FUCHSIA_DIR}/buildtools/ninja" ${concurrency_args} -C "${FUCHSIA_BUILD_DIR}" "$@"
  fi

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

  zboot "${FUCHSIA_BUILD_DIR}/user.bootfs" "$@"
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
    && zboot "${FUCHSIA_BUILD_DIR}/installer.bootfs" "$@"
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

  zrun -x "${FUCHSIA_BUILD_DIR}/user.bootfs" "$@"
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
  "${FUCHSIA_DIR}/zircon/scripts/symbolize" \
      --build-dir "${FUCHSIA_BUILD_DIR}" -- "$@"
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

### fmkzedboot: builds the Fuchsia bootloader and zeboot and places it on an
### external drive

function fmkzedboot-usage() {
  cat >&2 <<END
Usage: fmkzedboot <root of external drive>
Builds Fuchsia bootloader and zedboot and copies it to a drive of
your choice. Makes zedboot the default boot option.
END
}

function fmkzedboot() {
  if [[ $# -ne 1 ]]; then
    fmkzedboot-usage
    return 1
  fi

  fcheck || return 1

  if [[ ! -d $1 ]]; then
    echo >&2 "Drive at $1 does not appear to be mounted."
    return 1
  fi

  local DRIVE_DIR=$1
  local EFI_TARGET_DIR=$1/EFI/BOOT
  (
    set -e

    # Do a zircon build, and copy the bootloader to drive
    zbuild
    mkdir -p ${EFI_TARGET_DIR}
    cp ${ZIRCON_BUILD_DIR}/bootloader/bootx64.efi \
      ${EFI_TARGET_DIR}/BOOTX64.EFI

    # Make zedboot
    echo "netsvc.netboot=true virtcon.font=18x32" > ${ZIRCON_BUILD_DIR}/CMDLINE
    ${ZIRCON_BUILD_DIR}/tools/mkbootfs -o ${ZIRCON_BUILD_DIR}/zedboot.bin \
      ${ZIRCON_BUILD_DIR}/zircon.bin                                   \
      -C ${ZIRCON_BUILD_DIR}/CMDLINE ${ZIRCON_BUILD_DIR}/bootdata.bin

    # Copy zedboot.bin to drive
    cp ${ZIRCON_BUILD_DIR}/zedboot.bin ${DRIVE_DIR}

    # Set zedboot as the default boot option, with a timeout of 0
    # (i.e. boot instantly into zedboot)
    echo "bootloader.default=zedboot bootloader.timeout=0" > ${ZIRCON_BUILD_DIR}/CMDLINE
    cp ${ZIRCON_BUILD_DIR}/CMDLINE ${DRIVE_DIR}
  ) && \
    echo "Bootloader + zedboot loaded to $1"
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
    zbuild
    mkdir -p ${TARGET_DIR}
    cp ${ZIRCON_BUILD_DIR}/bootloader/bootx64.efi \
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
  fcheck || return 1
  SSH_AUTH_SOCK="" ssh -F $FUCHSIA_BUILD_DIR/ssh-keys/ssh_config $*
}

function fscp() {
  fcheck || return 1
  SSH_AUTH_SOCK="" scp -F $FUCHSIA_BUILD_DIR/ssh-keys/ssh_config $*
}

function fsftp() {
  fcheck || return 1
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

  fbuild "${target}"
  if [[ $? -ne 0 ]]; then
    return 1
  fi
  fcp "${FUCHSIA_BUILD_DIR}/${target}" "/tmp/${target}"
  shift
  fcmd "/tmp/${target}" $*
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

function fpublish-usage() {
  cat >&2 <<END
fpublish [--build-dir <DIR>] [--far-key <key file>] [--far-dir <DIR>] [--update-repo <DIR>] [pkg]
Publish packages. If no package name is supplied, all packages from the current
build output will be published.
  --build-dir
    Directory containing the build output
  --far-key
    Key used to sign the package's meta FAR
  --far-dir
    Directory to be used to build the meta FAR
  --update-repo
    Directory to be used to publish the meta FAR and associated content blobs
END
}

# Create a package manager package and then create update files which are
# published to the local file system. If no package name is supplied, all
# built packages are processed.
function fpublish() {
  local build_dir="${FUCHSIA_BUILD_DIR}"
  c=1
  while ((c<=$#)); do
    if [[ "${!c}" == "--build-dir" ]]; then
      c=$((c + 1))
      build_dir=${!c}
      break
    fi
    c=$((c + 1))
  done

  if [[ -z $build_dir ]]; then
    echo "Build directory is not set!"
    return -1
  fi

  # if an even number of args, assume no pkg name provided
  if [[ "$# % 2" -eq 0 ]]; then
    local pkgs_file="${build_dir}/gen/packages/gn/packages"
    local pkg_count=0
    local pkgs=()
    while IFS= read -r e || [[ -n "$e" ]]; do
      pkg_count=$(($pkg_count + 1))
      pkgs+=($e)
    done < "$pkgs_file"

    local i=1
    for e in "${pkgs[@]}"; do
      echo "Publishing ${i}/${pkg_count}: ${e}"
      fpublish-one "${@:1}" "$e"
      if [[ "$?" -ne 0 ]]; then
        fpublish-usage
        return $?
      fi
      i=$(($i + 1))
    done
  else
    fpublish-one "${@:1}"
      if [[ "$?" -ne 0 ]]; then
        fpublish-usage
        return $?
      fi
  fi
}

# See comments for fpublish, this does the same thing, but for an individual
# package.
function fpublish-one() {
  fcheck || return 1
  local pkg_name
  for pkg_name; do : ; done;
  local build_dir="${FUCHSIA_BUILD_DIR}"
  local stg_dir
  local update_repo

  local key_path
  local gen_key=1
  while (( "$#" )); do
    case $1 in
      "--update-repo")
        shift
        update_repo=$1
        ;;
      "--far-dir")
        shift
        stg_dir=$1
        ;;
      "--far-key")
        shift
        key_path=$1
        gen_key=0
        ;;
      "--build-dir")
        shift
        build_dir=$1
        ;;
    esac
    shift
  done

  if [[ -z $stg_dir ]]; then
    stg_dir="${build_dir}/fars/${pkg_name}"
  fi

  if [[ -z $update_repo ]]; then
    local update_repo="${build_dir}/amber-files"
  fi

  local arch_dir="${stg_dir}/archive"
  if [[ "$key_path" == "" ]]; then
    key_path="${stg_dir}/key"
  fi

  rm -r "${stg_dir}"/*  >/dev/null 2>&1
  mkdir -p "${arch_dir}"

  local pm_cmd="${build_dir}/host_x64/pm"
  local amber_cmd="${build_dir}/host_x64/amber-publish"
  local mani_path=""
  for try_path in ${build_dir}/package/${pkg_name}/{boot,system}_manifest; do
    if [[ -s  "$try_path" ]]; then
      mani_path="$try_path"
    fi
  done

  if [[ "$mani_path" == "" ]]; then
    echo "WARNING: manifest not found for ${pkg_name}, no package published."
    return 0
  fi

  if [[ "$gen_key" -eq 1 ]]; then
    "${pm_cmd}" "-o" "${arch_dir}" "-k" "${key_path}" "genkey" || return $?
  fi

  "${pm_cmd}" "-o" "${arch_dir}" "-n" "${pkg_name}" "init" || return $?

  "${pm_cmd}" "-o" "${arch_dir}" "-k" "${key_path}" "-m" "$mani_path" "build" || return $?

  "${amber_cmd}" "-r" "${update_repo}" "-p" "-f" "${arch_dir}/meta.far" "-n" "${pkg_name}.far" || return $?

  "${amber_cmd}" "-r" "${update_repo}" "-m" "-f" "${mani_path}" >/dev/null || return $?
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
