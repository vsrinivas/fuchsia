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
		Filename: "table_single_set.gen.gidl",
		Gen:      gidlGenTableSingleSet,
		Benchmarks: []config.Benchmark{
			{
				Name:    "Table/SingleSet/1_of_1",
				Comment: `Table with 1 field with the 1st field set`,
				Config: config.Config{
					"size":         1,
					"field_to_set": 1,
				},
				// Rust is removed from the allowlist because the benchmarks (not just this case) are timing out.
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.CPP, config.Go, config.Walker, config.Reference, config.Dart},
			},
			{
				Name:    "Table/SingleSet/1_of_16",
				Comment: `Table with 16 fields with the 1st field set`,
				Config: config.Config{
					"size":         16,
					"field_to_set": 1,
				},
				// Rust is removed from the allowlist because the benchmarks (not just this case) are timing out.
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.CPP, config.Go, config.Walker, config.Reference, config.Dart},
			},
			{
				Name:    "Table/SingleSet/1_of_63",
				Comment: `Table with 63 fields with the 1st field set`,
				Config: config.Config{
					"size":         63,
					"field_to_set": 1,
				},
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.CPP, config.Rust, config.Go, config.Walker, config.Reference, config.Dart},
			},
			{
				Name:    "Table/SingleSet/16_of_16",
				Comment: `Table with 16 fields with the 16th field set`,
				Config: config.Config{
					"size":         16,
					"field_to_set": 16,
				},
				// Rust is removed from the allowlist because the benchmarks (not just this case) are timing out.
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.CPP, config.Go, config.Walker, config.Reference, config.Dart},
			},
			{
				Name:    "Table/SingleSet/16_of_63",
				Comment: `Table with 63 fields with the 16th field set`,
				Config: config.Config{
					"size":         63,
					"field_to_set": 16,
				},
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.CPP, config.Rust, config.Go, config.Walker, config.Reference, config.Dart},
			},
			{
				Name:    "Table/SingleSet/63_of_63",
				Comment: `Table with 63 fields with the 64th field set`,
				Config: config.Config{
					"size":         63,
					"field_to_set": 63,
				},
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.CPP, config.Rust, config.Go, config.Walker, config.Reference, config.Dart},
			},
		},
	})
}

func gidlGenTableSingleSet(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	fieldToSet := conf.GetInt("field_to_set")
	return fmt.Sprintf(`
Table%[1]dStruct{
	value: Table%[1]d{
		field%[2]d: 1,
	},
}`, size, fieldToSet), nil
}
