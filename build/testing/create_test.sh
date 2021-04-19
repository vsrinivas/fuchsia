#!/bin/bash

# Copyright 2020 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# It's inconvenient to create scripts with executable permissions directly in
# GN, so this is a script that creates scripts. The generated scripts are
# wrappers for tests that call host tools with arguments.

set -o errexit

declare -r OUTFILE="${1}"
shift 1

declare -r TOOL="${1}"
shift 1

declare -a TOOL_ARGS
for arg in "$@"
do
  TOOL_ARGS+=("'${arg}'")
done

echo "#!/bin/bash" > "$OUTFILE"
echo "" >> "$OUTFILE"
echo "${TOOL} ${TOOL_ARGS[*]}" '"$@"' >> "$OUTFILE"
chmod a+x "$OUTFILE"
