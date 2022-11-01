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

// In LLCPP and possibly other bindings, vector size limits effect the buffer
// size that is allocated.
func init() {
	util.Register(config.GidlFile{
		Filename: "byte_vector_limit.gen.gidl",
		Gen:      gidlGenByteVectorLimit,
		Benchmarks: []config.Benchmark{
			{
				Name:    "ByteVectorLimit/1",
				Comment: `1 byte vector with a 1-element limit in a struct`,
				Config: config.Config{
					"limit": 1,
				},
			},
		},
	})
}

func gidlGenByteVectorLimit(conf config.Config) (string, error) {
	limit := conf.GetInt("limit")

	return fmt.Sprintf(`ByteVectorLimit%[1]d{
	bytes: [ 0 ]
}`, limit), nil
}
