// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package gidl

import (
	"fmt"
	"gen/config"
	"gen/gidl/util"
	"gen/types"
)

func init() {
	util.Register(config.GidlFile{
		Filename: "table_all_set.gen.gidl",
		Gen:      gidlGenTableAllSet,
		Benchmarks: []config.Benchmark{
			{
				Name:    "Table/AllSet/1",
				Comment: `Table with 1 field set`,
				Config: config.Config{
					"size": 1,
				},
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.Rust, config.Go, config.Walker, config.Reference, config.Dart},
			},
			{
				Name:    "Table/AllSet/16",
				Comment: `Table with 16 fields set`,
				Config: config.Config{
					"size": 16,
				},
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.Rust, config.Go, config.Walker, config.Reference, config.Dart},
			},
			{
				Name:    "Table/AllSet/256",
				Comment: `Table with 256 fields set`,
				Config: config.Config{
					"size": 256,
				},
				// Dart has a 256 argument limit which is exceeded by the table constructor.
				Allowlist: []config.Binding{config.LLCPP, config.HLCPP, config.Rust, config.Go, config.Walker, config.Reference},
			},
		},
	})
}

func gidlGenTableAllSet(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	return fmt.Sprintf(`
Table%[1]dStruct{
	value: Table%[1]d{
		%[2]s
	},
}`, size, util.Fields(1, size, "field", util.SequentialValues(types.Uint8, 1))), nil
}
