// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package fidl

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/config"
	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/fidl/util"
)

func init() {
	util.Register(config.FidlFile{
		Filename: "byte_vector_limit.gen.test.fidl",
		Gen:      fidlGenByteVectorLimit,
		Definitions: []config.Definition{
			{
				Config: config.Config{
					"limit": 1,
				},
			},
		},
	})
}

func fidlGenByteVectorLimit(config config.Config) (string, error) {
	limit := config.GetInt("limit")
	return fmt.Sprintf(`
type ByteVectorLimit%[1]d = struct{
	bytes vector<uint8>:%[1]d;
};`, limit), nil
}
