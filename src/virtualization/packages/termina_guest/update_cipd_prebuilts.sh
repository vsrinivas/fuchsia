#!/usr/bin/env bash

# Copyright 2019 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

set -o pipefail

declare -r TERMINA_GUEST_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
declare -r FUCHSIA_DIR="${TERMINA_GUEST_DIR}/../../../../"

# The revision of the prebuilt to fetch. The latest build revision can be read
# from:
#
# https://storage.googleapis.com/chromeos-image-archive/{tatl|tael}-full/LATEST-master
declare -r TERMINA_REVISION=R76-12182.0.0-rc1

print_usage_and_exit() {
  echo "Update prebuilt termina images from published Chrome images."
  echo ""
  echo "This script assists in updating the CIPD prebuilt images of the termina"
  echo "VM by downloading prebuilts from the chrome image archive, extracting"
  echo "them, then uploading them to CIPD."
  echo ""
  echo "Usage:"
  echo "  update_cipd_prebuilts.sh [-r <TERMINA_REVISION>] ( -n | -f )"
  echo ""
  echo "Where:"
  echo "   -r [TERMINA_REVISION] - Version of termina to publish to CIPD. This will"
  echo "      be a string that looks something like 'R76-12182.0.0-rc1'. If omitted,"
  echo "      the most recent version will be used."
  echo ""
  echo "   -n - Dry run. Don't actually upload anything, just show what would be"
  echo "      uploaded."
  echo ""
  echo "   -f - Force. Script will refuse to upload unless this flag is specified."

  exit $1
}

board_for_arch() {
  case "${1}" in
    arm64)
      echo tael-full ;;
    x64)
      echo tatl-full ;;
    *)
      >&2 echo "Unsupported arch ${1}; should be one of x64, arm64";
      exit -1;;
  esac
}

# Downloads and decompresses a prebuilt termina image.
#
# $1 - Architecture (x64 or arm64).
# $2 - Termina revision to use. To find the latest master build see
#      https://storage.googleapis.com/chromeos-image-archive/tatl-full/LATEST-master
#      or
#      https://storage.googleapis.com/chromeos-image-archive/tael-full/LATEST-master
# $3 - Directory to decompress the prebuilt image into.
fetch_and_decompress() {
  local -r arch="$1"
  local -r revision="$2"
  local -r outdir="$3"

  local -r board="`board_for_arch ${arch}`"

  curl -o "${outdir}/guest-vm-base.tbz" \
    "https://storage.googleapis.com/chromeos-image-archive/${board}/${revision}/guest-vm-base.tbz"
  tar xvf "${outdir}/guest-vm-base.tbz" --strip-components=1 -C "${outdir}"
  rm "${outdir}/guest-vm-base.tbz"
}

# Returns the most recent revision for a given board.
#
# $1 - Board requested.
latest_revision_for_board() {
  local -r board="$1"
  curl -s https://storage.googleapis.com/chromeos-image-archive/${board}/LATEST-master
}

main() {
  while getopts "r:nfh" FLAG; do
    case "${FLAG}" in
    r) termina_revision_requested="${OPTARG}" ;;
    n) dry_run=true ;;
    f) force=true ;;
    h) print_usage_and_exit 0 ;;
    *) print_usage_and_exit 1 ;;
    esac
  done
  shift $((OPTIND - 1))

  declare -r cipd="${FUCHSIA_DIR}/.jiri_root/bin/cipd"
  declare -r termina_revision_requested=${termina_revision_requested}
  declare -r dry_run=${dry_run}
  declare -r force=${force}
  declare jiri_entries="    <!-- termina guest images -->"

  # Ensure one of "dry-run" or "force" is given.
  if [ "$dry_run" == "$force" ];
  then
    print_usage_and_exit 1
  fi

  # Create a temporary directory to work in.
  local output_dir=$(mktemp -d)
  trap "rm -rf ${output_dir}" EXIT

  for arch in "x64" "arm64"
  do
    board=`board_for_arch ${arch}`
    termina_revision=${termina_revision_requested:-`latest_revision_for_board ${board}`}

    jiri_entries="${jiri_entries}
    <package name=\"fuchsia_internal/linux/termina-${arch}\"
             version=\"termina-upstream:${termina_revision}\"
             path=\"prebuilt/virtualization/packages/termina_guest/images/${arch}\"
             internal=\"true\"/>"

    # See if we've already uploaded this artifact to avoid having multiple
    # CIPD instances with the same tag.
    ${cipd} \
      search fuchsia_internal/linux/termina-${arch} \
      -tag "termina-upstream:${termina_revision}" | grep "fuchsia_internal/linux/termina-${arch}"
    if [[ $? -eq 0 ]];
    then
      echo "CIPD artifact already exists for ${termina_revision} on arch ${arch}; skipping upload."
      continue
    fi

    # Download and decompress the image.
    mkdir -p "${output_dir}/${arch}"
    fetch_and_decompress "${arch}" "${termina_revision}" "${output_dir}/${arch}"

    # Upload to CIPD.
    options=()
    options+=("create")
    options+=("-in" "${output_dir}/${arch}")
    options+=("-name" "fuchsia_internal/linux/termina-${arch}")
    options+=("-install-mode" "copy")
    options+=("-tag" "termina-upstream:${termina_revision}")
    echo "cipd ${options[*]}"
    if [ "$dry_run" != true ] ; then
      "${cipd}" ${options[*]}
    fi
  done

  echo "Update //integration/fuchsia/prebuilts with the following:"
  echo "${jiri_entries}"
}

main "$@"
