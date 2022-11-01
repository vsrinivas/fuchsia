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
		Filename: "table_all_unset.gen.gidl",
		Gen:      gidlGenTableAllUnset,
		Benchmarks: []config.Benchmark{
			{
				Name:    "Table/Unset/1",
				Comment: `Table with 1 field all unset`,
				Config: config.Config{
					"size": 1,
				},
				// Rust is removed from the allowlist because the benchmarks (not just this case) are timing out.
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.CPP, config.Go, config.Walker, config.Reference, config.Dart},
			},
			{
				Name:    "Table/Unset/16",
				Comment: `Table with 16 fields all unset`,
				Config: config.Config{
					"size": 16,
				},
				// Rust is removed from the allowlist because the benchmarks (not just this case) are timing out.
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.CPP, config.Go, config.Walker, config.Reference, config.Dart},
			},
			{
				Name:    "Table/Unset/63",
				Comment: `Table with 63 fields all unset`,
				Config: config.Config{
					"size": 63,
				},
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.CPP, config.Rust, config.Go, config.Walker, config.Reference, config.Dart},
			},
		},
	})
}

func gidlGenTableAllUnset(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	return fmt.Sprintf(`
Table%[1]dStruct{
	value: Table%[1]d{},
}`, size), nil
}
