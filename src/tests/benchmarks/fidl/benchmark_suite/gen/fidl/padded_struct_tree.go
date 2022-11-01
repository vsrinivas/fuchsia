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
		Filename: "padded_struct_tree.gen.test.fidl",
		Gen:      fidlGenPaddedStructTree,
		ExtraDefinition: `
type PaddedStructTree1 = struct{
	a uint8;
	// 3 byte padding
	b uint32;
};`,
		Definitions: []config.Definition{
			{
				Config: config.Config{
					"depth": 2,
				},
			},
			{
				Config: config.Config{
					"depth": 3,
				},
			},
			{
				Config: config.Config{
					"depth": 4,
				},
			},
			{
				Config: config.Config{
					"depth": 5,
				},
			},
			{
				Config: config.Config{
					"depth": 6,
				},
			},
			{
				Config: config.Config{
					"depth": 7,
				},
			},
			{
				Config: config.Config{
					"depth": 8,
				},
			},
		},
	})
}

func fidlGenPaddedStructTree(config config.Config) (string, error) {
	depth := config.GetInt("depth")
	return fmt.Sprintf(`
type PaddedStructTree%[1]d = struct{
	left PaddedStructTree%[2]d;
	right PaddedStructTree%[2]d;
};`, depth, depth-1), nil
}
