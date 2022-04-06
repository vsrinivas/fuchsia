#!/bin/bash
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script adds @available attributes to all SDK FIDL libraries, for the
# introduction of FIDL Versioning.

set -euo pipefail

initial_api_level=7
temp_file=$(mktemp)

# Adds attribute in file $1 which belongs to FIDL library $2.
add_attribute_to_file() {
    platform=
    [[ "$2" != "fuchsia."* ]] && platform='platform="fuchsia", '
    sed -i "s/^library $2;$/@available(${platform}added=$initial_api_level)\n&/" "$1"
}

# Finds the first file in $@ that has a doc comment on the library declaration,
# prints it (with a preceding blank line), and removes it from the file.
extract_doc_comment() {
    for file in "$@"; do
        if rg -qU '^///.*\nlibrary ' "$file"; then
            echo
            gawk -v temp_file="$temp_file" '
!lib && /^\/\/\// { print; next; }
/^library / { lib = 1; }
{ print > temp_file; }
END { if (!lib) exit 1; }
' "$file"
            mv "$temp_file" "$file"
            return
        fi
    done
}

# Adds $1 to the list of sources in the BUILD.gn file $2 for FIDL library $3.
add_to_sources() {
    gawk -i inplace -v new_file="$1" -v library="$3" '
m == 0 && $0 ~ "^fidl\\(\"" library "\"\\)" { m = 1; }
m == 1 && match($0, /^( +sources = \[)(.*)$/, a) {
    print a[1] "\"" new_file "\"," a[2]; m = 2; next;
}
{ print; }
END { if (m != 2) { print FILENAME > "/dev/tty"; exit 1; } }
' "$2"
}

cd "$FUCHSIA_DIR"

for dir in sdk/{fidl,banjo}/fuchsia.* sdk/banjo/ddk.hw.physiter; do
    library=$(basename "$dir")
    # This is the only case with two FIDL libraries in the same BUILD.gn.
    # Neither are exposed in the SDK so we can skip it.
    [[ "$library" == "fuchsia.power.battery" ]] && continue
    # This library has no .fidl files and should likely be removed.
    [[ "$library" == "fuchsia.device.vsock" ]] && continue
    # Ensure there is only one fidl target.
    if [[ "$(rg '^fidl\("' "$dir/BUILD.gn" -c)" -ne 1 ]]; then
        echo "unexpected BUILD.gn: $dir/BUILD.gn" >&2
        exit 1
    fi
    # Only add the attribute if sdk_category is "partner".
    if ! rg -qF 'sdk_category = "partner"' "$dir/BUILD.gn"; then
        # Exception: We must include fuchsia.device and fuchsia.ui.focus even
        # though they're "internal" because "partner" libraries depend on them.
        case $library in
            fuchsia.device) ;;
            fuchsia.ui.focus) ;;
            *) continue ;;
        esac
    fi
    files=()
    while read -r f; do
        files+=("$f")
    done < <(find "$dir" -type f -name "*.fidl")
    # For the overview.fidl convention see:
    # https://fuchsia.dev/fuchsia-src/development/languages/fidl/guides/style#library_overview
    main_file="$dir/overview.fidl"
    if ! [[ -f "$main_file" ]]; then
        if [[ "${#files[@]}" -eq 1 ]]; then
            main_file=${files[0]}
        else
            cat <<EOS > "$main_file"
// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
$(extract_doc_comment "${files[@]}")
library $library;
EOS
            add_to_sources "$(basename "$main_file")" "$dir/BUILD.gn" "$library"
        fi
    fi
    add_attribute_to_file "$main_file" "$library"
done

fx format-code
