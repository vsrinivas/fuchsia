// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package fidl

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/config"
	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/fidl/util"
	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/types"
)

func init() {
	util.Register(config.FidlFile{
		Filename: "struct_field.gen.test.fidl",
		Gen:      fidlGenStructField,
		Definitions: []config.Definition{
			{
				Config: config.Config{
					"size": 16,
				},
			},
			{
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

func fidlGenStructField(config config.Config) (string, error) {
	size := config.GetInt("size")
	return fmt.Sprintf(`
type StructField%[1]d = struct{
%[2]s
};`, size, util.StructFields(types.Uint8, "field", size)), nil
}
