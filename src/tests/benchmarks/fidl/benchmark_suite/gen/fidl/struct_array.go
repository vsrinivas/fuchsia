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
		Filename: "struct_array.gen.test.fidl",
		Gen:      fidlGenStructArray,
		ExtraDefinition: `
struct StructArrayElement {
	uint8 a;
	// 8 byte padding.
	uint16 b;
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

func fidlGenStructArray(config config.Config) (string, error) {
	size := config.GetInt("size")
	return fmt.Sprintf(`
struct StructArray%[1]d {
	array<StructArrayElement>:%[1]d elems;
};`, size), nil
}
