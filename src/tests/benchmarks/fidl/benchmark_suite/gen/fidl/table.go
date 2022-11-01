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
		Filename: "table.gen.test.fidl",
		Gen:      fidlGenTable,
		Definitions: []config.Definition{
			{
				Config: config.Config{
					"size": 1,
				},
			},
			{
				Config: config.Config{
					"size": 16,
				},
			},
			{
				Config: config.Config{
					"size": 63,
				},
			},
		},
	})
}

func fidlGenTable(config config.Config) (string, error) {
	size := config.GetInt("size")
	return fmt.Sprintf(`
type Table%[1]dStruct = struct{
	value Table%[1]d;
};

type Table%[1]d = table{
	%[2]s
};`, size, util.OrdinalFields(types.Uint8, "field", size)), nil
}
