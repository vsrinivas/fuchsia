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
		Filename: "struct_vector.gen.gidl",
		Gen:      gidlGenStructVector,
		Benchmarks: []config.Benchmark{
			{
				Name:    "StructVector/256",
				Comment: `256 element vector of structs`,
				Config: config.Config{
					"size": 256,
				},
			},
		},
	})
}

func gidlGenStructVector(conf config.Config) (string, error) {
	size := conf.GetInt("size")

	elem := `
StructVectorElement{
	a: 1,
	b: 2
},`

	return fmt.Sprintf(`
StructVector{
	elems: [
%[1]s
	]
}`, strings.Repeat(elem, size)), nil
}
