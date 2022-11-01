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
		Filename: "enum_array_non_dense.gen.gidl",
		Gen:      gidlGenEnumArrayNonDense,
		Benchmarks: []config.Benchmark{
			{
				Name:    "EnumArray/NonDense/256",
				Comment: `array of 256 enums that cannot be validated with a range`,
				Config: config.Config{
					"size": 256,
				},
			},
		},
	})
}

func gidlGenEnumArrayNonDense(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	if size%2 != 0 {
		panic("expected even size")
	}

	return fmt.Sprintf(`
EnumArrayNonDense%[1]d{
	values: [
%[2]s
	]
}`, size, strings.Repeat("1,\n3,\n", size/2)), nil
}
