// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package fidl

import (
	"fmt"
	"gen/config"
	"gen/fidl/util"
)

func init() {
	util.Register(config.FidlFile{
		Filename: "padded_struct_tree.gen.test.fidl",
		Gen:      fidlGenPaddedStructTree,
		ExtraDefinition: `
struct PaddedStructTree1 {
	uint8 a;
	// 3 byte padding
	uint32 b;
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
struct PaddedStructTree%[1]d {
	PaddedStructTree%[2]d left;
	PaddedStructTree%[2]d right;
};`, depth, depth-1), nil
}
