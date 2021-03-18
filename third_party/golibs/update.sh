#!/usr/bin/env bash

# Copyright 2021 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script updates the list of external dependencies in imports.go, which
# allows `go mod` to operate in that directory to update and vendor those
# dependencies.

set -euo pipefail

cd "$(dirname "$0")"

for file in go.mod go.sum; do
  destination=$FUCHSIA_DIR/$file
  [ -f "$destination" ] || ln -s "$FUCHSIA_DIR"/third_party/golibs/$file "$destination"
done

GOROOTBIN=$FUCHSIA_DIR/prebuilt/third_party/go/linux-x64/bin
GO=$GOROOTBIN/go
GOFMT=$GOROOTBIN/gofmt

IMPORTS=()
for dir in $FUCHSIA_DIR $FUCHSIA_DIR/third_party/cobalt; do
  while IFS='' read -r line; do IMPORTS+=("$line"); done < <(cd "$dir" && git ls-files -- \
    '*.go' ':!third_party/golibs/vendor' |
    xargs dirname |
    sort | uniq |
    sed 's|^|./|' |
    xargs "$GO" list -mod=readonly -e -f \
      '{{join .Imports "\n"}}{{"\n"}}{{join .TestImports "\n"}}{{"\n"}}{{join .XTestImports "\n"}}' |
    grep -vF go.fuchsia.dev/fuchsia/ |
    # Apparently we generate these normally checked-in files?
    grep -vF 'go.chromium.org/luci' |
    # TODO(https://fxbug.dev/35565): Remove once cobalt imports the canonical path.
    grep -vF 'github.com/go-yaml/yaml' |
    grep -F . |
    sort | uniq |
    xargs "$GO" list -mod=readonly -e -f \
      '{{if not .Goroot}}_ "{{.ImportPath}}"{{end}}' |
    sort | uniq)
done

IMPORTS_STR=$(
  IFS=$'\n'
  echo "${IMPORTS[*]}"
)

printf '// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package imports

import (\n%s\n)' "$IMPORTS_STR" | $GOFMT -s >imports.go

# Move jiri-managed repositories out of the module.
TMP=$(mktemp -d)
git check-ignore * | xargs -I % mv % "$TMP"/%
function cleanup() {
  mv "$TMP"/* .
}
trap cleanup EXIT

$GO get -u gvisor.dev/gvisor@go
$GO get -u
$GO mod tidy
$GO mod vendor
