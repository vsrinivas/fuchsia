#!/bin/bash
# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script is used to implement the "copy()" command.
# See //build/toolchain/default_tools.gni

set -e

PROGDIR="$(dirname "$0")"

PYTHON_EXE=
HELP=

die () {
  echo "ERROR: $*" >&2
  exit 1
}

ARGS=()
for OPT; do
  case "${OPT}" in
    --python-exe=*)
      PYTHON_EXE="${OPT##--python-exe=}"
      ;;
    --help)
      HELP=true
      ;;
    -*)
      die "Invalid option $OPT, see --help."
      ;;
    *)
      ARGS+=("${OPT}")
      ;;
  esac
done

if [[ "${HELP}" ]]; then
  cat <<EOF
Usage: $PROGNAME [options] SOURCE DESTINATION

Copy SOURCE into DESTINATION as efficiently as possible while preserving
mtime with the highest accuracy possible (some platform copy commands
truncate the mtime, e.g. on OS X, the nanoseconds are truncated to
microseconds).

Valid options:
  --help               Print this message.
  --python-exe=PYTHON  Path to Python interpreter to use, required on OS X.
fi
EOF
  exit 0
fi

if [[ "${#ARGS[@]}" != "2" ]]; then
  die "This script requires two arguments! See --help."
fi

SOURCE="${ARGS[0]}"
DESTINATION="${ARGS[1]}"

# mtime on directories may not reflect the latest of a directory. For example in
# popular file systems, modifying a file in a directory does not affect mtime of
# the directory. Because of this, disable directory copy for incremental
# correctness. See https://fxbug.dev/73250.
if [[ -d "${SOURCE}" ]]; then
  die "Tool \"copy\" does not support directory copies"
fi

# When copying symlinks with relative paths to parent directories, create
# a new symlink to the same destination file but with a new correct relative
# path to it.
if [[ -L "${SOURCE}" ]]; then
  LINK="$(readlink "${SOURCE}")"
  if [[ "${LINK##../}" != "${LINK}" ]]; then
    ln -sf "$(realpath --relative-to=$(dirname "${DESTINATION}")" "$(realpath "${SOURCE}"))" "${DESTINATION}"
    exit 0
  fi
fi

# We use link instead of copy by default; the way "copy" tool is being used is
# compatible with links since Ninja is tracking changes to the source.
ln -f "${SOURCE}" "${DESTINATION}" 2>/dev/null && exit 0

# Hard-linking failed, which can happen if the source and destination
# are not on the same partition, or if the filesystem does not support
# hard links, so fall back to copying.

# On Mac, `cp -af` does not correctly preserves mtime (the nanoseconds are
# truncated to microseconds) which causes spurious ninja rebuilds. As a result,
# shell to a helper to copy rather than calling cp -r. See https://fxbug.dev/56376#c5
case "${OSTYPE}" in
  darwin*)
    "${PYTHON_EXE}" "${PROGDIR}/copy.py" "${SOURCE}" "${DESTINATION}"
    ;;
  *)
    rm -f "${DESTINATION}" && cp -af "${SOURCE}" "${DESTINATION}"
    ;;
esac
