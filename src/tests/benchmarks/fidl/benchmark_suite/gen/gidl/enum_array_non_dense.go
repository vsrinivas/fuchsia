// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gidl

import (
	"fmt"
	"gen/config"
	"gen/gidl/util"
	"strings"
)

func init() {
	util.Register(config.GidlFile{
		Filename: "enum_array_non_dense.gen.gidl",
		Gen:      gidlGenEnumArrayNonDense,
		Benchmarks: []config.Benchmark{
			{
				Name: "EnumArray/NonDense/256",
				Comment: `array of 256 enums that cannot be validated with a range
				Disabled on LLCPP / Walker because of enum bug in GIDL`,
				Config: config.Config{
					"size": 256,
				},
				Denylist: []config.Binding{config.LLCPP, config.Walker},
			},
		},
	})
}

func gidlGenEnumArrayNonDense(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	if size%2 != 0 {
		panic("expected even size")
	}

	return fmt.Sprintf(`
EnumArrayNonDense%[1]d{
	values: [
%[2]s
	]
}`, size, strings.Repeat("1,\n3,\n", size/2)), nil
}
