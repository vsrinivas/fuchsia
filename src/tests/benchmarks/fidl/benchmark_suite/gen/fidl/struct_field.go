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
struct StructField%[1]d {
%[2]s
};`, size, util.StructFields(types.Uint8, "field", size)), nil
}
