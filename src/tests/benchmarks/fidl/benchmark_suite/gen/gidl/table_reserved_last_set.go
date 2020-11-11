// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package gidl

import (
	"fmt"
	"gen/config"
	"gen/gidl/util"
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
				Name:    "Table/LastSetOthersReserved/256",
				Comment: `Table with 255 reserved fields and one non-reserved set field`,
				Config: config.Config{
					"size": 256,
				},
				// Dart has a 256 argument limit which is exceeded by the table constructor.
				Denylist: []config.Binding{config.Dart},
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
