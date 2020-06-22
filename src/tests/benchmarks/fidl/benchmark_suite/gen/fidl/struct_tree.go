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
		Filename: "struct_tree.gen.test.fidl",
		Gen:      fidlGenStructTree,
		ExtraDefinition: `
struct StructTree1 {
	uint8 a;
	uint8 b;
};`,
		Definitions: []config.Definition{
			{
				Config: config.Config{
					"depth": 2,
				},
				Denylist: []config.Binding{config.Dart},
			},
			{
				Config: config.Config{
					"depth": 3,
				},
				Denylist: []config.Binding{config.Dart},
			},
			{
				Config: config.Config{
					"depth": 4,
				},
				Denylist: []config.Binding{config.Dart},
			},
			{
				Config: config.Config{
					"depth": 5,
				},
				Denylist: []config.Binding{config.Dart},
			},
			{
				Config: config.Config{
					"depth": 6,
				},
				Denylist: []config.Binding{config.Dart},
			},
			{
				Config: config.Config{
					"depth": 7,
				},
				Denylist: []config.Binding{config.Dart},
			},
			{
				Config: config.Config{
					"depth": 8,
				},
				Denylist: []config.Binding{config.Dart},
			},
		},
	})
}

func fidlGenStructTree(config config.Config) (string, error) {
	depth := config.GetInt("depth")
	return fmt.Sprintf(`
struct StructTree%[1]d {
	StructTree%[2]d left;
	StructTree%[2]d right;
};`, depth, depth-1), nil
}
