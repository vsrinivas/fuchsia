// Copyright 2020 The Fuchsia Authors. All rights reserved.
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
		Filename: "struct_tree.gen.gidl",
		Gen:      gidlGenStructTree,
		Benchmarks: []config.Benchmark{
			{
				Name:    "StructTree/Depth8",
				Comment: `Binary tree with depth 8 (255 elements)`,
				Config: config.Config{
					"depth": 8,
				},
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.CPP, config.Rust, config.Go, config.Walker, config.Reference, config.Dart},
			},
			{
				Name:    "StructTree/Depth6",
				Comment: `Binary tree with depth 6 (63 elements)`,
				Config: config.Config{
					"depth": 6,
				},
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.CPP, config.Rust, config.Go, config.Walker, config.Reference, config.Dart},
			},
			{
				Name:    "StructTree/Depth4",
				Comment: `Binary tree with depth 4 (15 elements)`,
				Config: config.Config{
					"depth": 4,
				},
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.CPP, config.Rust, config.Go, config.Walker, config.Reference, config.Dart},
			},
		},
	})
}

func treeValueString(level int) string {
	if level == 1 {
		return `StructTree1{
	a: 1,
	b: 2,
}`
	}
	nextLevel := treeValueString(level - 1)
	return fmt.Sprintf(
		`StructTree%[1]d{
		left:%[2]s,
		right:%[2]s,
	}`, level, nextLevel)
}

func gidlGenStructTree(conf config.Config) (string, error) {
	depth := conf.GetInt("depth")
	return treeValueString(depth), nil
}
