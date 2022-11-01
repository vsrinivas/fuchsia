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

func init() {
	util.Register(config.GidlFile{
		Filename: "table_reserved_last_set.gen.gidl",
		Gen:      gidlGenTableReservedLastSet,
		Benchmarks: []config.Benchmark{
			{
				Name:    "Table/LastSetOthersReserved/16",
				Comment: `Table with 15 reserved fields and one non-reserved set field`,
				Config: config.Config{
					"size": 16,
				},
			},
			{
				Name:    "Table/LastSetOthersReserved/63",
				Comment: `Table with 62 reserved fields and one non-reserved set field`,
				Config: config.Config{
					"size": 63,
				},
			},
		},
	})
}

func gidlGenTableReservedLastSet(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	return fmt.Sprintf(`
TableReserved%[1]dStruct{
	value: TableReserved%[1]d{
		field%[1]d: 1,
	},
}`, size), nil
}
