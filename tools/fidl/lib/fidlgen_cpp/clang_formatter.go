// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// NewClangFormatter a formatter that invokes clang-format.
// TODO(fxbug.dev/49757) Use --style=file and copy the .clang-format file to the correct location.
// An alternate way to do this is to load the config directly from .clang_format and put the
// style as JSON in quotes.
func NewClangFormatter(clangFormatPath string) fidl.Formatter {
	if clangFormatPath != "" {
		return fidl.NewFormatter(clangFormatPath, "--style=google")
	}
	// Don't format if path isn't specified.
	return fidl.NewFormatter("")
}
