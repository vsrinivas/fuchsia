// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cpp

import (
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
)

// NewClangFormatter a formatter that invokes clang-format.
// TODO(fxbug.dev/49757) Use --style=file and copy the .clang-format file to the correct location.
// An alternate way to do this is to load the config directly from .clang_format and put the
// style as JSON in quotes.
func NewClangFormatter(clangFormatPath string) common.Formatter {
	if clangFormatPath != "" {
		return common.NewFormatter(clangFormatPath, "--style=google")
	}
	// Don't format if path isn't specified.
	return common.NewFormatter("")
}
