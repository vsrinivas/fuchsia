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
		Filename: "table_reserved.gen.test.fidl",
		Gen:      fidlGenTableReserved,
		Definitions: []config.Definition{
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

func fidlGenTableReserved(config config.Config) (string, error) {
	size := config.GetInt("size")
	return fmt.Sprintf(`
type TableReserved%[1]dStruct = struct{
	value TableReserved%[1]d;
};

type TableReserved%[1]d = table{
	%[2]s
	%[1]d: field%[1]d uint8;
};`, size, util.ReservedFields(1, size-1)), nil
}
