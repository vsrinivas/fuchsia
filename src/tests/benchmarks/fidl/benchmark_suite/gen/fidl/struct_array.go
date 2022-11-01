// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package fidl

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/config"
	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/fidl/util"
)

func init() {
	util.Register(config.FidlFile{
		Filename: "struct_array.gen.test.fidl",
		Gen:      fidlGenStructArray,
		ExtraDefinition: `
type StructArrayElement = struct{
	a uint8;
	// 8 byte padding.
	b uint16;
};`,
		Definitions: []config.Definition{
			{
				Config: config.Config{
					"size": 256,
				},
			},
		},
	})
}

func fidlGenStructArray(config config.Config) (string, error) {
	size := config.GetInt("size")
	return fmt.Sprintf(`
type StructArray%[1]d = struct{
	elems array<StructArrayElement, %[1]d>;
};`, size), nil
}
