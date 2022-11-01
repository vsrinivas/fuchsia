// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package gidl

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/config"
	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/gidl/util"
	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/types"
)

func init() {
	util.Register(config.GidlFile{
		Filename: "struct_field.gen.gidl",
		Gen:      gidlGenStructField,
		Benchmarks: []config.Benchmark{
			{
				Name:    "StructField/16",
				Comment: `Struct with 16 uint8 fields`,
				Config: config.Config{
					"size": 16,
				},
			},
			{
				Name:    "StructField/256",
				Comment: `Struct with 256 uint8 fields`,
				Config: config.Config{
					"size": 256,
				},
				// Dart has a 256 argument limit and all struct fields are passed into
				// the constructor.
				Denylist: []config.Binding{config.Dart},
			},
		},
	})
}

func gidlGenStructField(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	return fmt.Sprintf(`
StructField%[1]d{
%[2]s
}`, size, util.Fields(1, size, "field", util.SequentialValues(types.Uint8, 0))), nil
}
