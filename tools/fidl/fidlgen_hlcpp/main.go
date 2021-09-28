// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_hlcpp/codegen"
	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

func main() {
	flags := cpp.NewCmdlineFlags("hlcpp", []string{"natural-types"}, true)
	fidl := flags.ParseAndLoadIR()
	generator := codegen.NewGenerator(flags)
	generator.GenerateFiles(fidl, []string{"Header", "Implementation"})
	if !flags.ExperimentEnabled("natural-types") {
		generator.GenerateFiles(fidl, []string{"TestBase"})
	}
}
