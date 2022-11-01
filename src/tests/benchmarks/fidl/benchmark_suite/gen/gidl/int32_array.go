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
		Filename: "int32_array.gen.gidl",
		Gen:      gidlGenInt32Array,
		Benchmarks: []config.Benchmark{
			{
				Name:    "Int32Array/256",
				Comment: `256 element int32 array in a struct`,
				Config: config.Config{
					"size": 256,
				},
				Denylist: []config.Binding{config.Rust},
			},
		},
	})
}

func gidlGenInt32Array(conf config.Config) (string, error) {
	size := conf.GetInt("size")

	return fmt.Sprintf(`
Int32Array%[1]d{
	bytes: [
%[2]s
	]
}`, size, util.List(size, util.SequentialHexValues(types.Int32, 0))), nil
}
