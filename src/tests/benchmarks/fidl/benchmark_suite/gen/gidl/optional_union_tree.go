// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package gidl

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/config"
	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/gidl/util"
)

func init() {
	util.Register(config.GidlFile{
		Filename: "optional_union_tree.gen.gidl",
		Gen:      gidlGenOptionalUnionTree,
		Benchmarks: []config.Benchmark{
			{
				Name:    "OptionalUnionTree/Depth8",
				Comment: `A linked list of unions with depth 8 composed of recursive optional unions`,
				Config: config.Config{
					"depth": 8,
				},
				// Go runs into a stack overflow.
				// Dart doesn't support recursive type generation.
				Denylist: []config.Binding{config.Go, config.Dart},
			},
			{
				Name:    "OptionalUnionTree/Depth6",
				Comment: `A linked list of unions with depth 6 composed of recursive optional unions`,
				Config: config.Config{
					"depth": 6,
				},
				Denylist: []config.Binding{config.Go, config.Dart},
			},
			{
				Name:    "OptionalUnionTree/Depth4",
				Comment: `A linked list of unions with depth 4 composed of recursive optional unions`,
				Config: config.Config{
					"depth": 4,
				},
				Denylist: []config.Binding{config.Dart},
			},
		},
	})
}

func optionalUnionTreeValueString(level int) string {
	if level == 0 {
		return `null`
	}
	nextLevel := optionalUnionTreeValueString(level - 1)
	return fmt.Sprintf(
		`OptionalUnionTree{
		s: OptionalUnionTreeWrapper {
			d: %[1]s,
		}
	}`, nextLevel)
}

func gidlGenOptionalUnionTree(conf config.Config) (string, error) {
	depth := conf.GetInt("depth")
	union := optionalUnionTreeValueString(depth)
	return fmt.Sprintf(
		`OptionalUnionTreeWrapper{
			d: %[1]s,
		}`, union), nil
}
