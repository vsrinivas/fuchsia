// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
				Denylist: []config.Binding{config.Rust},
			},
			{
				Config: config.Config{
					"depth": 3,
				},
				Denylist: []config.Binding{config.Rust},
			},
			{
				Config: config.Config{
					"depth": 4,
				},
				Denylist: []config.Binding{config.Rust},
			},
			{
				Config: config.Config{
					"depth": 5,
				},
				Denylist: []config.Binding{config.Rust},
			},
			{
				Config: config.Config{
					"depth": 6,
				},
				Denylist: []config.Binding{config.Rust},
			},
			{
				Config: config.Config{
					"depth": 7,
				},
				Denylist: []config.Binding{config.Rust},
			},
			{
				Config: config.Config{
					"depth": 8,
				},
				Denylist: []config.Binding{config.Rust},
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
