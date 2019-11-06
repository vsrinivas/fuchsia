#!/bin/bash

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Usage:
# generate_fidl_json.sh \
#   --qjc=<JS interpreter> \
#   --out-file=<output manifest file> \
#   [--fidl-json-file=<optional file listing FIDL JSON IR files>] \
#   <FIDL JSON IR files>

# Given a JavaScript interpreter, a list of FIDL JSON IR files, and a
# destination file, this code generates a manifest that can be used for a
# Fuchsia package that contains those FIDL JSON IR files.  The directories
# containing the FIDL JSON IR files will be structured based on the name of the
# library, so, for example, library fuchsia.foo.bar will be in
# fuchsia/foo/bar.fidl.json.  This makes lookup very quick.

while (( $# > 0 )); do
  case "$1" in
    --qjs=*)
      QJS="${1#--qjs=}"
      ;;
    --out-file=*)
      OUT_FILE="${1#--out-file=}"
      ;;
    --fidl-json-file=*)
      FIDL_JSON_FILE="${1#--fidl-json-file=}"
      ;;
    *)
      break
      ;;
  esac
  shift
done

if [[ -z "${QJS}" || -z "${OUT_FILE}" ]]; then
  echo "Parameters to generate_fidl_json are incorrect."
  echo "--qjs=$QJS --out-file=$OUT_FILE --fidl-json-file=$FIDL_JSON_FILE"
  exit 1
fi

rm -rf "${OUT_FILE}"

# Parses the JSON to get the library name.
function get_name() {
  local file=$1
  "${QJS}" --std -e "const f = std.open(\"${file}\", 'r'); \
          var str = f.readAsString(); \
          f.close(); \
	  var regex = /(\"ordinal\"\\s*:\\s*)([0-9]+)\\s*,/gi; \
	  str = str.replace(regex, '\$1\"\$2\",'); \
	  var ir = JSON.parse(str); \
	  std.printf(ir.name);"
}

if [[ -n "${FIDL_JSON_FILE}" ]]; then
  for file in $(cat "${FIDL_JSON_FILE}"); do
    # Create the file in a path derivable from its library name, for easy lookup.
    NAME=$(get_name "${file}")
    FILE_LOCATION=$(echo ${NAME} | sed -e "s|\.|\/|g").fidl.json
    echo  "data/fidling/${FILE_LOCATION}=${file}" >> "${OUT_FILE}"
  done
fi

while (( $# > 0 )); do
  file=$1
  shift
  NAME=$(get_name "${file}")
  FILE_LOCATION=$(echo ${NAME} | sed -e "s|\.|\/|g").fidl.json
  echo  "data/fidling/${FILE_LOCATION}=${file}" >> "${OUT_FILE}"
done
