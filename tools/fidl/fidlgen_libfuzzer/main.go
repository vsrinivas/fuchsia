// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_libfuzzer/codegen"
	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

func main() {
	flags := cpp.NewCmdlineFlags("libfuzzer", nil, false)
	fidl := flags.ParseAndLoadIR()
	generator := codegen.NewGenerator(flags)
	generator.GenerateFiles(fidl, []string{"Header", "Source",
		"DecoderEncoderHeader", "DecoderEncoderSource"})
}
