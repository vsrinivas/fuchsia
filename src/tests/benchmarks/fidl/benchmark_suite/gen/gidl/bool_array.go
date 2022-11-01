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
		Filename: "bool_array.gen.gidl",
		Gen:      gidlGenBoolArray,
		Benchmarks: []config.Benchmark{
			{
				Name:    "BoolArray/256",
				Comment: `256 bool array in a struct`,
				Config: config.Config{
					"size": 256,
				},
			},
		},
	})
}

func gidlGenBoolArray(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	if size%2 != 0 {
		panic("expected even size")
	}

	return fmt.Sprintf(`
BoolArray%[1]d{
	values: [
%[2]s
	]
}`, size, strings.Repeat("true,\nfalse,\n", size/2)), nil
}
