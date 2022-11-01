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
		Filename: "enum_array_non_dense.gen.test.fidl",
		Gen:      fidlGenEnumArrayNonDense,
		ExtraDefinition: `
type EnumArrayNonDenseElement = enum{
	A = 1;
	C = 3;
	D = 6;
	E = 8;
	F = 12;
	G = 13;
	H = 14;
	J = 16;
};`,
		Definitions: []config.Definition{
			{
				Config: config.Config{
					"size": 256,
				},
			},
		},
	})
}

func fidlGenEnumArrayNonDense(config config.Config) (string, error) {
	size := config.GetInt("size")
	return fmt.Sprintf(`
type EnumArrayNonDense%[1]d = struct{
	values array<EnumArrayNonDenseElement, %[1]d>;
};`, size), nil
}
