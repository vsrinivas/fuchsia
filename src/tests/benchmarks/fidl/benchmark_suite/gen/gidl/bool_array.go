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
		Filename: "bool_array.gen.gidl",
		Gen:      gidlGenBoolArray,
		Benchmarks: []config.Benchmark{
			{
				Name:    "BoolArray/16",
				Comment: `16 bool array in a struct`,
				Config: config.Config{
					"size": 16,
				},
			},
			{
				Name:    "BoolArray/256",
				Comment: `256 bool array in a struct`,
				Config: config.Config{
					"size": 256,
				},
			},
			{
				Name: "BoolArray/4096",
				Comment: `
			4096 bool array in a struct
			Disabled on HLCPP / LLCPP due to clang performance issues`,
				Config: config.Config{
					"size": 4096,
				},
				Denylist: []config.Binding{config.HLCPP, config.LLCPP, config.Rust, config.Walker},
			},
		},
	})
}

func gidlGenBoolArray(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	if size%2 != 0 {
		panic("expected even size")
	}

	return fmt.Sprintf(`
BoolArray%[1]d{
	values: [
%[2]s
	]
}`, size, strings.Repeat("true,\nfalse,\n", size/2)), nil
}
