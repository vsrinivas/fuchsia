// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_llcpp/codegen"
	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

func main() {
	flags := cpp.NewCmdlineFlags("llcpp", nil)
	fidl := flags.ParseAndLoadIR()
	generator := codegen.NewGenerator(flags)
	generator.GenerateFiles(fidl, []string{
		"WireHeader", "UnifiedHeader", "WireTestBase", "Markers",
		"CommonTypesHeader",
		"WireTypesHeader", "WireTypesSource",
		"WireMessagingHeader", "WireMessagingSource",
		"NaturalTypesHeader", "NaturalTypesSource",
		"NaturalMessagingHeader", "NaturalMessagingSource",
		"driver/Header",
		"driver/MessagingHeader", "driver/MessagingSource",
	})
}
