#!/bin/bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -euf

rust="$(cd "$(dirname "$0")" && pwd)/fix_rust_exhaustive_tables.rs"

cd "$FUCHSIA_DIR"

# Temporarily rename .cargo, otherwise rust-script will get messed up by reading
# .cargo/config and using the Fuchsia prebuilt rustc.
mv .cargo .cargo.bak
trap 'mv .cargo.bak .cargo' EXIT

while fx build | tee build.log | gawk '
/^error\[E0063\]: missing field `__non_exhaustive` in initializer of `.*`$/ {
    if (mode != 0) exit 1;
    mode = 10; next_nr = NR + 1; next;
}
/^error\[E0027\]: pattern does not mention field `__non_exhaustive`$/ {
    if (mode != 0) exit 1;
    mode = 20; next_nr = NR + 1; next;
}
mode == 10 && /^\s*-->/ {
    if (NR != next_nr) exit 1;
    print "con," $2 ","; mode = 0; next;
}
mode == 20 && /^\s*-->/ {
    if (NR != next_nr) exit 1;
    mode = 21; path = $2; next;
}
mode == 21 && /^help: if you don.t care about this missing field/ {
    mode = 22; next_nr = NR + 2; next;
}
mode == 22 && /^\s*[0-9]+\s*\|.*/ {
    if (NR != next_nr) exit 1;
    print "pat," path "," $0; mode = 0; next;
}
/in this macro invocation/ {
    mode = 0;
}
' | sort -u | tee gawk.log | rust-script "$rust"; do
    gawk -F, '{print $2}' gawk.log \
        | sed 's|^\.\./\.\./||;s|:.*$||;' \
        | sort -u \
        | xargs sed -i 's|/\*INSERT_NEWLINE\*/|\n|g'
done

fx format-code
