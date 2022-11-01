// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package zither

import (
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// HeaderPath gives the relative path at which to emit .h files for a given C
// family backend.
func HeaderPath(summary FileSummary, backend string) string {
	return filepath.Join("lib", summary.Library.String(), backend, summary.Name()+".h")
}

// HeaderGuard gives the header guard value for a C family backend.
func HeaderGuard(summary FileSummary, backend string) string {
	path := HeaderPath(summary, backend)
	// TODO(fxbug.dev/110021, fxbug.dev/111453): Once these headers are no
	// longer checked in, we can drop these rewrite rules.
	if backend == "c" || backend == "asm" {
		switch summary.Library.String() {
		case "zx":
			path = "SYSROOT_ZIRCON_" + summary.Name() + "_H"
		case "zbi":
			path = "SYSROOT_ZIRCON_BOOT_" + summary.Name() + "_H"
		}
	}
	for _, c := range []string{".", string(filepath.Separator), "-"} {
		path = strings.ReplaceAll(path, c, "_")
	}
	return fidlgen.ConstNameToAllCapsSnake(path) + "_"
}
