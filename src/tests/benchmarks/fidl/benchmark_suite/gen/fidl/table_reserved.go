// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package fidl

import (
	"fmt"
	"gen/config"
	"gen/fidl/util"
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
					"size": 256,
				},
				// Dart has a 256 argument limit which is exceeded by the table constructor.
				Denylist: []config.Binding{config.Dart},
			},
		},
	})
}

func fidlGenTableReserved(config config.Config) (string, error) {
	size := config.GetInt("size")
	return fmt.Sprintf(`
struct TableReserved%[1]dStruct {
	TableReserved%[1]d value;
};

table TableReserved%[1]d {
	%[2]s
	%[1]d: uint8 field%[1]d;
};`, size, util.ReservedFields(1, size-1)), nil
}
