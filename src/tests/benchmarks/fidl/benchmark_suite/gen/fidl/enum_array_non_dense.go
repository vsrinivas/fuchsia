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
	D = 6;
	E = 8;
	F = 12;
	G = 13;
	H = 14;
	J = 16;
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

func fidlGenEnumArrayNonDense(config config.Config) (string, error) {
	size := config.GetInt("size")
	return fmt.Sprintf(`
struct EnumArrayNonDense%[1]d {
	array<EnumArrayNonDenseElement>:%[1]d values;
};`, size), nil
}
