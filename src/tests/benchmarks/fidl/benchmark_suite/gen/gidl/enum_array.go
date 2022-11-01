// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package gidl

import (
	"fmt"
	"strings"

	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/config"
	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/gidl/util"
)

func init() {
	util.Register(config.GidlFile{
		Filename: "enum_array.gen.gidl",
		Gen:      gidlGenEnumArray,
		Benchmarks: []config.Benchmark{
			{
				Name:    "EnumArray/256",
				Comment: `256 enum array in a struct`,
				Config: config.Config{
					"size": 256,
				},
			},
		},
	})
}

func gidlGenEnumArray(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	if size%2 != 0 {
		panic("expected even size")
	}

	return fmt.Sprintf(`
EnumArray%[1]d{
	values: [
%[2]s
	]
}`, size, strings.Repeat("1,\n2,\n", size/2)), nil
}
