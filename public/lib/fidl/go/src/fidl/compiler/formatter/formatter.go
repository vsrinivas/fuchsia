// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package formatter

import (
	"fidl/compiler/core"
	"fidl/compiler/parser"
)

// FormatMojom accepts the source code for a mojom file and returns a
// semantically-equivalent pretty-printed version of that file.
// |filename| will be used to construct parser error messages.
// If |source| cannot be parsed, a non-nil error will be returned. The
// returned error has been formatted to be displayed to the user.
func FormatMojom(filename, source string) (formatted string, err error) {
	descriptor := core.NewMojomDescriptor()
	parser := parser.MakeParser(filename, filename, source, descriptor, nil)
	parser.Parse()

	if !parser.OK() {
		return "", parser.GetError()
	}

	mojomFile := parser.GetMojomFile()
	comments := parser.GetComments()
	core.AttachCommentsToMojomFile(mojomFile, comments)

	printer := newPrinter()
	printer.writeMojomFile(mojomFile)
	return printer.result(), nil
}
