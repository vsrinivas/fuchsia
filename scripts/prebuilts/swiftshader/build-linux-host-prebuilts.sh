#!/bin/sh
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# A script used to rebuild Vulkan host Linux prebuilts with the Fuchsia toolchain.
set -e
export LANG=C
export LC_ALL=C

readonly SCRIPT_NAME="$(basename $0)"
readonly SCRIPT_DIR="$(dirname $0)"

# Print an error message to stderr then exit the script immediately.
die () {
  echo 2>&1 "$*"
  exit 1
}

# Return true if $1 is an absolute file path
is_absolute_path () {
  [[ "${1#/}" != "$1" ]]
}

# Get absolute directory path
# $1: directory path, must exist!
# Out: absolute path of $1
absolute_path () {
  (cd "$1" && pwd)
}

# Set this variable to ensure that the corresponding directory
# is always removed when this script exits, even in case of error!
REMOVE_TMPDIR_ON_EXIT=

# Internal trap function used to ensure proper cleanups on script exit.
_on_exit () {
  if [[ -n "$REMOVE_TMPDIR_ON_EXIT" ]]; then
    rm -rf "$REMOVE_TMPDIR_ON_EXIT"
  fi
  return 0
}

trap _on_exit EXIT QUIT HUP

# Sanity checks
#
# Note that this script only works on Linux (i.e not OS X) because
# building the Vulkan-Loader requires running a host Linux executable generated
# during the build.
#
# It might be possible to support building on OS X by first performing a
# host build of the Vulkan-Loader only, followed by a cross-build to Linux
# with adequate PATH manipulation, but this use case is not important enough
# for now to support.
if [[ "$(uname)" != "Linux" ]]; then
  die "ERROR: This script only works on a Linux build machine!"
fi

readonly DEFAULT_BUILD_CONFIG_FILE="${SCRIPT_DIR}/build_config"

DO_HELP=
DO_HELP_USAGE=
TOP_BUILD_DIR=
INSTALL_DIR=
DEST_INSTALL_DIR=
TOOLCHAIN_FILE=
BUILD_CONFIG_FILE=
NO_CCACHE=
NO_CLEAN_BUILD=
GENERATOR=

GIT_MIRRORS=()

git_mirror_src_prefix () {
  printf %s "$1" | cut -d, -f1
}

git_mirror_dst_prefix () {
  printf %s "$1" | cut -d, -f2
}

# Add a --git-mirror to the global list, while sanity checking the value.
add_git_mirror () {
  local SRC_PREFIX="$(git_mirror_src_prefix "$1")"
  local DST_PREFIX="$(git_mirror_dst_prefix "$1")"
  if [[ -z "${DST_PREFIX}" ]]; then
    die "--git-mirror value should use colon as delimiter: $1"
  fi
  if [[ -z "${SRC_PREFIX}" ]]; then
    die "--git-mirror value is invalid!"
  fi
  GIT_MIRRORS+=($1)
}

# Rewrite URL if needed through global list of git mirrors.
# $1: input URL
# out: output URL
rewrite_git_url () {
  local URL="$1"
  local MIRROR SRC_PREFIX DST_PREFIX
  for MIRROR in "${GIT_MIRRORS[@]}"; do
    SRC_PREFIX="$(git_mirror_src_prefix "${MIRROR}")"
    DST_PREFIX="$(git_mirror_dst_prefix "${MIRROR}")"
    if [[ "${URL}" == "${SRC_PREFIX}"* ]]; then
      # echo >&2 "XXX ${URL} -> ${DST_PREFIX} | ${SRC_PREFIX}"
      URL="${DST_PREFIX}${URL##${SRC_PREFIX}}"
      break
    fi
  done
  printf %s "${URL}"
}

# The following variables are taken from the environment, or can be overriden
# with --<name>=<value> options below.
GIT="${GIT:-git}"
GIT_OPTS="${GIT_OPTS:-}"
CMAKE="${CMAKE:-cmake}"
CMAKE_OPTS="${CMAKE_OPTS:-}"
MAKE="${MAKE:-make}"
NINJA="${NINJA:-ninja}"
ENABLE_DEBUGGER_SUPPORT=

for OPT; do
  case "${OPT}" in
    --help|-?)
      DO_HELP=true
      ;;
    --help-usage)
      DO_HELP_USAGE=usage
      ;;
    --build-dir=*)
      TOP_BUILD_DIR="${OPT##--build-dir=}"
      ;;
    --install-dir=*)
      INSTALL_DIR="${OPT##--install-dir=}"
      ;;
    --dest-install-dir=*)
      DEST_INSTALL_DIR="${OPT##--dest-install-dir=}"
      ;;
    --cmake-toolchain-file=*)
      TOOLCHAIN_FILE="${OPT##--cmake-toolchain-file=}"
      ;;
    --build-config-file=*)
      BUILD_CONFIG_FILE="${OPT##--build-config-file=}"
      ;;
    --no-ccache)
      NO_CCACHE=true
      ;;
    --no-clean-build)
      NO_CLEAN_BUILD=true
      ;;
    --generator=*)
      GENERATOR="${OPT##--generator=}"
      ;;
    --git=*)
      GIT="${OPT##--git=}"
      ;;
    --git-opt=*)
      GIT_OPTS="${OPT##--git-opt=}"
      ;;
    --cmake=*)
      CMAKE="${OPT##--cmake=}"
      ;;
    --cmake-opts=*)
      CMAKE_OPTS="${OPT##--cmake-opts=}"
      ;;
    --make=*)
      MAKE="${OPT##--make=}"
      ;;
    --ninja=*)
      NINJA="${OPT##--ninja=}"
      ;;
    --enable-debugger-support)
      ENABLE_DEBUGGER_SUPPORT=true
      ;;
    --git-mirror=*)
      add_git_mirror "${OPT##--git-mirror=}"
      ;;
    -*)
      die "Unknown option ${OPT}, see --help for supported ones."
  esac
done

if [[ -n "${DO_HELP}" ]]; then
  cat <<EOF
Usage: ${PROGNAME} [options]

Rebuild prebuilt host Linux binaries for SwiftShader and the Vulkan loader,
using the Fuchsia prebuilt toolchain and sysroot. The corresponding binaries
can then be used to run graphics tests on infra bots that don't have a GPU,
(see --help-usage for complete details).

Valid options:
  --help|-?           Print this message

  --help-usage        Print detailed usage instructions for this script
                      and the generated binaries.

  --install-dir=<DIR> REQUIRED: Specify local installation directory
                      for generated binaries and data files.

  --dest-install-dir=<DIR>
                      Destination installation directory. This is the
                      intended final installation location for the
                      binaries, which can be different from --install-dir
                      (e.g. when copying them to a different machine).
                      MUST BE an absolute path.

  --build-config-file=<FILE>
                      Configuration file listing the exact git url and
                      revisions to use for the build. By default, this is:
                      ${DEFAULT_BUILD_CONFIG_FILE}

  --generator=<GENERATOR>
                      Specify generation build tool. Valid values are
                      'make' or 'ninja'. By default, use ninja if available,
                      or fallback to make.

  --no-ccache         Disable ccache use. By default, if 'ccache' is
                      in your path, it will be used to rebuild all
                      C and C++ binaries.

  --git-mirror=<SRC_PREFIX>,<DST_PREFIX>
                      Ensure git urls beginning with SRC_PREFIX will be
                      replaced by ones beginning with DST_PREFIX.
                      Can be called multiple times to support
                      several mirrors.

The following options are used to specify the path and options to programs
invoked by this script. Their default value are taken from environment
variables if defined, or have a simple fallback.

  --git=<PROGRAM>     Specify git executable to use. Default is GIT environment
                      variable, or 'git'.

  --git-opts=<LIST>   Space-separated list of options to the git program.
                      Default is taken from the GIT_OPTS environment variable,
                      or an empty list as a fallback.

  --cmake=<PROGRAM>   Specify CMake executable to use. Default is CMAKE
                      environment variable, or 'cmake'.

  --cmake-opts=<LIST> Space-separated list of options to the CMake program.
                      Default from CMAKE_OPTS environment variable, or empty
                      list as a fallback.

  --make=<PROGRAM>    Specify make executable to use. Default is MAKE
                      environment variable, or 'make'. Ignored if ninja is used.

  --ninja=<PROGRAM>   Specify ninja executable to use. Default is NINJA
                      environment variable, or 'ninja'. Ignored if make is used.

The following options are only useful when debugging this script's
operations, and should not be used to produce release binaries:

  --build-dir=<DIR>   Specify persistent build directory. By default,
                      a temporary directory with a random path under
                      /tmp will be used (and deleted on exit).

  --no-clean-build    Disable clean builds.

  --cmake-toolchain-file=<FILE>
                      Specify an alternative CMake toolchain file for
                      this build. The default uses the Fuchsia prebuilt
                      toolchain and Linux sysroot to ensure the generated
                      binaries run on as many Linux distributions as
                      possible.

  --enable-debugger-support
                      Enable SwiftShader debugger support. This feature
                      is still experimental and disabled by default in
                      the official build.

EOF
  exit 0
fi

if [[ -n "${DO_HELP_USAGE}" ]]; then
  cat <<EOF
This script will rebuild binaries required to run Vulkan graphics tests
on a Linux machine that has no GPU, using SwiftShader as the Vulkan driver.
Note that this requires an X server at the moment to run properly.

The binaries should be portable to many Linux distributions.

Complete usage instructions are:

  1) Build and install the sources from scratch using this script, e.g.:

      INSTALL_DIR=\$HOME/vulkan-swiftshader
      ${SCRIPT_NAME} --install-dir=\${INSTALL_DIR}

 2) Source the \${INSTALL_DIR}/env_vars.sh file, it will set several
    environment variables to ensure the right Vulkan libraries and
    data files are used (feel free to look or modify this file if needed).

 3) Ensure the DISPLAY environment variable is set to point to
    an X11 server, if you don't have one, install Xvfb and start
    it as a background service, e.g.:

      # For installation only.
      sudo apt-get install xvfb

      # Start Xvfb server on display port 99
      Xvfb :99 &
      export DISPLAY=:99

 4) Run your program.

A good way to check that you have a working environment is to run
the "vulkaninfo" program to verify that you are using SwiftShader as
the Vulkan driver/ICD, e.g.:

   \$ vulkaninfo 2>/dev/null| grep deviceName
           deviceName     = SwiftShader Device (LLVM 7.0.1)

Special use cases:

  * Use --dest-install-dir=<DIR> it you intend to install the binaries to
    a different final location (e.g when copying them to a remote machine).
    For example:

      # Generate and install locally
      INSTALL_DIR=\$HOME/vulkan-swiftshader
      DEST_INSTALL_DIR=/opt/vulkan-swiftshader
      ${SCRIPT_NAME} \\
          --install-dir=\${INSTALL_DIR} \\
          --dest-install-dir=\${DEST_INSTALL_DIR}

      # Copy to final location:
      scp -r \${INSTALL_DIR} remote-machine:\${DEST_INSTALL_DIR}

EOF
  exit 0
fi

if [[ -z "${TOP_BUILD_DIR}" ]]; then
  TOP_BUILD_DIR="/tmp/fuchsia-vulkan-host-prebuilts-$$"
  REMOVE_TMPDIR_ON_EXIT="${TOP_BUILD_DIR}"
else
  # The SwiftShader build currently fails when the build install prefix
  # contains a space! Detect this early!
  if [[ "${TOP_BUILD_DIR}" =~ " " ]]; then
    die "ERROR: Build directory cannot contain spaces: [${TOP_BUILD_DIR}]"
  fi
fi

readonly SOURCES_DIR="${TOP_BUILD_DIR}/sources"

if [[ -z "${INSTALL_DIR}" ]]; then
  die "ERROR: This script requires an --install-dir=<path> argument!"
fi

if [[ -z "${DEST_INSTALL_DIR}" ]]; then
  mkdir -p "${INSTALL_DIR}"
  DEST_INSTALL_DIR="$(absolute_path "${INSTALL_DIR}")"
else
  if ! is_absolute_path "${DEST_INSTALL_DIR}"; then
    die "ERROR: --dest-install-dir argument must be absolute path: ${DEST_INSTALL_DIR}"
  fi
fi

if [[ -z "${TOOLCHAIN_FILE}" ]]; then
  TOOLCHAIN_FILE="$(absolute_path "${SCRIPT_DIR}/../../..")/build/cmake/HostLinuxToolchain.cmake"
fi

if [[ ! -f "${TOOLCHAIN_FILE}" ]]; then
  die "ERROR: Missing CMake toolchain file: ${TOOLCHAIN_FILE}"
fi

if [[ -z "${BUILD_CONFIG_FILE}" ]]; then
  BUILD_CONFIG_FILE="${DEFAULT_BUILD_CONFIG_FILE}"
fi
if [[ ! -f "${BUILD_CONFIG_FILE}" ]]; then
  die "ERROR: Missing build configuration file: ${BUILD_CONFIG_FILE}"
fi

source "${BUILD_CONFIG_FILE}"

# Temporary binaries are installed in this directory. This includes
# development headers, libraries, etc. Only a subset of them will be
# copied to the real installation directory specified by INSTALL_DIR
readonly INSTALL_PREFIX="${TOP_BUILD_DIR}/build-install"

# NOTE: For some reason, trying to use MinRelSize for the build type ends up
#       with binaries that are far larger than Release (e.g. 43 MiB vs 23 MiB
#       for libVkLayer_khronos_validation.so, or 95 MiB vs 29 MiB for
#       libvk_swiftshader.so !!)
COMMON_CMAKE_FLAGS=(
  "-DCMAKE_BUILD_TYPE=Release"
  "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"
  "-DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX}"
)

# Auto-detect ccache and use it when available
if [[ -z "${NO_CCACHE}" ]]; then
  readonly CCACHE="$(which ccache 2>/dev/null || true)"
  if [[ -n "${CCACHE}" ]]; then
    COMMON_CMAKE_FLAGS+=("-DUSE_CCACHE=1")
  fi
fi

# The command used to build and install binaries to ${INSTALL_PREFIX}
readonly MAKE_JOBS="$(nproc)"

readonly GIT_PROGRAM="$(which "${GIT}" 2>/dev/null || true)"
if [[ -z "${GIT_PROGRAM}" ]]; then
  die "ERROR: Cannot find git executable (${GIT})"
fi

readonly GIT_CMD=("${GIT_PROGRAM}" ${GIT_OPTS})

readonly CMAKE_PROGRAM="$(which "${CMAKE}" 2>/dev/null || true)"
if [[ -z "${CMAKE_PROGRAM}" ]]; then
  die "ERROR: Cannot find 'cmake' executable (${CMAKE})"
fi

readonly CMAKE_CMD=("${CMAKE_PROGRAM}" ${CMAKE_OPTS})

readonly NINJA_PROGRAM="$(which "${NINJA}" 2>/dev/null || true)"
readonly MAKE_PROGRAM="$(which "${MAKE}" 2>/dev/null || true)"

if [[ -z "${GENERATOR}" ]]; then
  if [[ -n "${NINJA_PROGRAM}" ]]; then
    GENERATOR=ninja
  elif [[ -n "${MAKE_PROGRAM}" ]]; then
    GENERATOR=make
    echo >&2 "WARNING: Using 'make' to build binaries, using 'ninja' would speed up the build considerably!"
  else
    die "ERROR: Neither 'make' or 'ninja' are in your path!"
  fi
fi

case "${GENERATOR}" in
ninja)
  if [[ -z "${NINJA_PROGRAM}" ]]; then
    die "ERROR: Missing ninja executable (${NINJA})"
  fi
  COMMON_CMAKE_FLAGS+=(-GNinja)
  readonly BUILD_INSTALL_CMD=("${NINJA_PROGRAM}" "-j${MAKE_JOBS}" install)
  ;;
make)
  if [[ -z "${MAKE_PROGRAM}" ]]; then
    die "ERROR: Missing make executable (${MAKE})"
  fi
  readonly BUILD_INSTALL_CMD=("${MAKE_PROGRAM}" "-j${MAKE_JOBS}" install)
  ;;
*)
  die "ERROR: Invalid --generator value [${GENERATOR}], must be one of: make ninja"
  ;;
esac


# Clone or update a git repository
# $1: Target directory
# $2: URL (can include revision after a '@' separator)
clone_or_update () {
  local URL="$(printf %s "$2" | cut -s -d@ -f1)"
  local REVISION="$(printf %s "$2" | cut -s -d@ -f2)"
  local DIR="$1"
  if [[ -z "${URL}" ]]; then
    URL="$2"
  fi
  URL="$(rewrite_git_url "${URL}")"
  echo "Cloning from: $URL..."
  if [[ ! -d "${DIR}" ]]; then
    "${GIT_CMD[@]}" clone ${URL} "${DIR}"
    if [[ "${REVISION}" ]]; then
      "${GIT_CMD[@]}" -C "${DIR}" checkout --quiet "${REVISION}"
    fi
#     if [[ -f "${DIR}/.gitmodules" ]]; then
#       "${GIT_CMD[@]}" -C "${DIR}" submodule update --init
#     fi
  else
    if [[ "${REVISION}" ]]; then
      "${GIT_CMD[@]}" -C "${DIR}" fetch
      "${GIT_CMD[@]}" -C "${DIR}" checkout --quiet "${REVISION}"
    else
      "${GIT_CMD[@]}" -C "${DIR}" pull
    fi
#     if [[ -f "${DIR}/.gitmodules" ]]; then
#       "${GIT_CMD[@]}" -C "${DIR}" submodule update
#     fi
  fi
}

# $1: Source directory.
# $2: Target directory.
symlink_dir () {
  # When using --build-dir, the linked might already exist.
  if [[ -d "$1" ]]; then
    rm -rf "$1"
  fi
  mkdir -p "$(dirname "$1")"
  ln -sf "$2" "$1"
}

# Build a source directory with CMake.
# Make sure that any dependencies have been built and installed before.
#
# This performs a clean build, and installs the binaries to ${INSTALL_PREFIX}
#
# $1: Source directory
# $2+: Extra cmake arguments
build_with_cmake () {
  local SRC_DIR="$(absolute_path "$1")"
  local BUILD_DIR="${TOP_BUILD_DIR}/build-$(basename ${SRC_DIR})"
  if [[ -z "${NO_CLEAN_BUILD}" ]]; then
    rm -rf "${BUILD_DIR}"
  fi
  (mkdir -p "${BUILD_DIR}" && cd "${BUILD_DIR}" && "${CMAKE_CMD[@]}" "${SRC_DIR}" "${COMMON_CMAKE_FLAGS[@]}" "$@" && "${BUILD_INSTALL_CMD[@]}")
}

# Combine git repository clone/update and cmake build.
# Useful for most projects that don't need special setup between these two
# steps.
# $1: Source directory
# $2: git URL
# $3+: Extra cmake arguments
clone_and_build () {
  local SRC_DIR="$1"
  local URL="$2"
  shift
  shift
  clone_or_update "${SRC_DIR}" "${URL}"
  build_with_cmake ${SRC_DIR} "$@"
}

#
# Swiftshader dependencies.
#
if [[ -n "${ENABLE_DEBUGGER_SUPPORT}" ]]; then
  CPPDAP_DIR="${SOURCES_DIR}/cppdap"
  clone_or_update "${CPPDAP_DIR}" "${CPPDAP_GIT_URL}"

  JSON_DIR="${SOURCES_DIR}/json"
  clone_or_update "${JSON_DIR}" "${JSON_GIT_URL}"

  LIBBACKTRACE_DIR="${SOURCES_DIR}/libbacktrace"
  clone_or_update "${LIBBACKTRACE_DIR}" "${LIBBACKTRACE_GIT_URL}"
fi

#
# SwiftShader - no dependencies
#
readonly SWIFTSHADER_DIR="${SOURCES_DIR}/SwiftShader"
clone_or_update "${SWIFTSHADER_DIR}" "${SWIFTSHADER_GIT_URL}"

DEBUGGER_SUPPORT_FLAG=
if [[ -n "${ENABLE_DEBUGGER_SUPPORT}" ]]; then
  symlink_dir "${SWIFTSHADER_DIR}/third_party/cppdap" "${CPPDAP_DIR}"
  symlink_dir "${SWIFTSHADER_DIR}/third_party/json" "${CPPDAP_DIR}"
  symlink_dir "${SWIFTSHADER_DIR}/third_party/libbacktrace/src" "${LIBBACKTRACE_DIR}"
  DEBUGGER_SUPPORT_FLAG="-DSWIFTSHADER_ENABLE_DEBUGGER=1"
fi


build_with_cmake "${SWIFTSHADER_DIR}" \
  -DSWIFTSHADER_BUILD_TESTS=OFF \
  -DSWIFTSHADER_BUILD_SAMPLES=OFF \
  -DSWIFTSHADER_BUILD_EGL=0 \
  -DSWIFTSHADER_BUILD_GLESv2=0 \
  -DSWIFTSHADER_BUILD_GLES_CM=0 \
  -DSWIFTSHADER_BUILD_VULKAN=1 \
  -DSWIFTSHADER_WARNINGS_AS_ERRORS=0 \
  ${DEBUGGER_SUPPORT_FLAG}

# Required because "make install" copies into $BUILD_DIR/Linux/ instead.
# The executable and .json files are relocatable, but must be in the same
# directory. Use VK_ICD_FILENAMES=${INSTALL_PREFIX}/lib/vk_swiftshader_icd.json
# to use the SwiftShader ICD with the Vulkan loader.
cp "${TOP_BUILD_DIR}"/build-SwiftShader/Linux/{libvk_swiftshader.so,vk_swiftshader_icd.json} "${INSTALL_PREFIX}/lib"

#
# Vulkan-Headers - no dependencies
#
readonly VULKAN_HEADERS_DIR="${SOURCES_DIR}/Vulkan-Headers"
clone_and_build "${VULKAN_HEADERS_DIR}" "${VULKAN_HEADERS_GIT_URL}"
readonly VULKAN_HEADERS_CMD=("-DVULKAN_HEADERS_INSTALL_DIR=${INSTALL_PREFIX}")

#
# Vulkan-Loader - depends on Vulkan-Headers
#
readonly VULKAN_LOADER_DIR="${SOURCES_DIR}/Vulkan-Loader"
clone_and_build "${VULKAN_LOADER_DIR}" "${VULKAN_LOADER_GIT_URL}" \
  -DBUILD_TESTS=OFF \
  -DBUILD_WSI_WAYLAND_SUPPORT=OFF \
  "${VULKAN_HEADERS_CMD[@]}"

#
# glslang - no dependencies
#
readonly GLSLANG_DIR="${SOURCES_DIR}/glslang"
clone_and_build "${GLSLANG_DIR}" "${GLSLANG_GIT_URL}"
readonly GLSLANG_CMD=("-DGLSLANG_INSTALL_DIR=${INSTALL_PREFIX}")

#
# effcee
#
readonly EFFCEE_DIR="${SOURCES_DIR}/effcee"
clone_or_update "${EFFCEE_DIR}" "${EFFCEE_GIT_URL}"

#
# RE2
#
readonly RE2_DIR="${SOURCES_DIR}/re2"
clone_or_update "${RE2_DIR}" "${RE2_GIT_URL}"

#
# SPIRV-Headers - no dependencies
#
readonly SPIRV_HEADERS_DIR="${SOURCES_DIR}/SPIRV-Headers"
clone_and_build "${SPIRV_HEADERS_DIR}" "${SPIRV_HEADERS_GIT_URL}"

#
# SPIRV-Tools - depends on SPIRV-Headers, effcee & re2
#
readonly SPIRV_TOOLS_DIR="${SOURCES_DIR}/SPIRV-Tools"
clone_or_update "${SPIRV_TOOLS_DIR}" "${SPIRV_TOOLS_GIT_URL}"

# Special setup of the external sub-directory
symlink_dir "${SPIRV_TOOLS_DIR}/external/spirv-headers" "${SPIRV_HEADERS_DIR}"
symlink_dir "${SPIRV_TOOLS_DIR}/external/effcee" "${EFFCEE_DIR}"
symlink_dir "${SPIRV_TOOLS_DIR}/external/re2" "${RE2_DIR}"

build_with_cmake "${SPIRV_TOOLS_DIR}" -DSPIRV_SKIP_TESTS=ON

#
# Vulkan-ValidationLayers - depends on Vulkan-Headers + glslang + SPIRV-Tools
#
# Note that when cross-compiling, the CMake probing will not find the installed
# SPIRV-Tools libraries (probably due to pkg-config issues), so force them
# through CMake variables below.
#
readonly VULKAN_VALIDATION_LAYERS="${SOURCES_DIR}/Vulkan-ValidationLayers"
clone_and_build "${VULKAN_VALIDATION_LAYERS}" "${VULKAN_VALIDATION_LAYERS_GIT_URL}" \
    -DBUILD_TESTS=OFF \
    -DBUILD_WSI_WAYLAND_SUPPORT=OFF \
    "${VULKAN_HEADERS_CMD[@]}" "${GLSLANG_CMD[@]}" \
    -DSPIRV_HEADERS_INSTALL_DIR="${INSTALL_PREFIX}" \
    -DSPIRV_TOOLS_LIB="${INSTALL_PREFIX}/lib/libSPIRV-Tools.a" \
    -DSPIRV_TOOLS_OPT_LIB="${INSTALL_PREFIX}/lib/libSPIRV-Tools-opt.a"

# Finally, copy files of interest to their final location
#
# NOTE: Both libvulkan.so and libvulkan.so.1 are symlinks.
#
# Executables are linked with "libvulkan.so.1", but the vulkan.hpp header may
# call dlopen on "libvulkan.so". On some systems, it will crash if these two
# files are not the same one.
#
# So we make libvulkan.so.1 a copy of the dynamic library it links to, and
# make libvulkan.so a symbolic link to libvulkan.so.1.
#
if [[ ! -f "${INSTALL_PREFIX}/lib/libvulkan.so.1" ]]; then
  echo "Fatal: libvulkan.so.1 not found!" >&2
  exit 1
fi
TMP_FILE=`mktemp -p ${INSTALL_PREFIX}`
cp -f "${INSTALL_PREFIX}/lib/libvulkan.so.1" "${TMP_FILE}"
mv -f "${TMP_FILE}" "${INSTALL_PREFIX}/lib/libvulkan.so.1"
chmod 640 "${INSTALL_PREFIX}/lib/libvulkan.so.1"
ln -sf "libvulkan.so.1" "${INSTALL_PREFIX}/lib/libvulkan.so"

FILES=(
  lib/libvk_swiftshader.so
  lib/vk_swiftshader_icd.json
  lib/libvulkan.so.1
  lib/libvulkan.so
  lib/libVkLayer_khronos_validation.so
  share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json
)

mkdir -p "${INSTALL_DIR}" &&
(tar c -f- -C "${INSTALL_PREFIX}" --preserve-permissions "${FILES[@]}") | (tar x -f- --overwrite -C "${INSTALL_DIR}")

# Copy the build configuration file to the installation directory as well, for reference.
cp "${BUILD_CONFIG_FILE}" "${INSTALL_DIR}/swiftshader_vulkan.build_config"

# Generate a small shell script that sets all necessary environment variables
# NOTE: The output below is designed to be sourced from any user shell and
#       cannot use Bash-specific features like `[[` instead of `[`.
cat > "${INSTALL_DIR}/env_vars.sh" <<EOF
# Auto-generated - Source this file in your environment to use these Vulkan
# binaries.

# Modify LD_LIBRARY_PATH to find libvulkan.so here.
# NOTE: An empty item in a PATH variable is treated as the current directory,
# which is often undesirable for security reasons. And this happens when
# using a leading or trailing column (i.e. PATH=:/bin  really means
# "search in current directory, then in /bin").
if [ -z "\$LD_LIBRARY_PATH" ]; then
  export LD_LIBRARY_PATH="${DEST_INSTALL_DIR}/lib"
else
  LD_LIBRARY_PATH="${DEST_INSTALL_DIR}/lib:\$LD_LIBRARY_PATH"
fi

# Set VK_LAYER_PATH to force the Vulkan loader to use the rigth layers
export VK_LAYER_PATH="${DEST_INSTALL_DIR}/share/vulkan/explicit_layer.d"

# Set VK_ICD_FILENAMES to force the Vulkan loader to use SwiftShader.
export VK_ICD_FILENAMES="${DEST_INSTALL_DIR}/lib/vk_swiftshader_icd.json"

if [ -z "\$DISPLAY" ]; then
  echo >&2 "WARNING: Define DISPLAY environment variable to run Vulkan programs!"
fi
EOF

echo "Done! Remember to source ${INSTALL_DIR}/env_vars.sh before running Vulkan programs."
