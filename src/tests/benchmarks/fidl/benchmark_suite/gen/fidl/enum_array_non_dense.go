// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidl

import (
	"fmt"
	"gen/config"
	"gen/fidl/util"
)

func init() {
	util.Register(config.FidlFile{
		Filename: "enum_array_non_dense.gen.test.fidl",
		Gen:      fidlGenEnumArrayNonDense,
		ExtraDefinition: `
enum EnumArrayNonDenseElement{
	A = 1;
	C = 3;
};`,
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
			},
			{
				Config: config.Config{
					"size": 4096,
				},
				// The Rust bindings only supports arrays of size 0-32, 64, and 256.
				// CPP generated code is slow to compile in clang.
				Denylist: []config.Binding{config.Rust, config.HLCPP, config.LLCPP, config.Walker},
			},
		},
	})
}

func fidlGenEnumArrayNonDense(config config.Config) (string, error) {
	size := config.GetInt("size")
	return fmt.Sprintf(`
struct EnumArrayNonDense%[1]d {
	array<EnumArrayNonDenseElement>:%[1]d values;
};`, size), nil
}
