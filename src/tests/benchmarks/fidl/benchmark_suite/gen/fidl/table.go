// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package fidl

import (
	"fmt"
	"gen/config"
	"gen/fidl/util"
	"gen/types"
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
					"size": 256,
				},
				// Dart has a 256 argument limit and all set table fields are passed into
				// the constructor.
				Denylist: []config.Binding{config.Dart},
			},
		},
	})
}

func fidlGenTable(config config.Config) (string, error) {
	size := config.GetInt("size")
	return fmt.Sprintf(`
struct Table%[1]dStruct {
	Table%[1]d value;
};

table Table%[1]d {
	%[2]s
};`, size, util.OrdinalFields(types.Uint8, "field", size)), nil
}
