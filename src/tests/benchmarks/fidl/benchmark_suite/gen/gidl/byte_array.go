// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gidl

import (
	"fmt"
	"gen/config"
	"gen/gidl/util"
	"gen/types"
)

func init() {
	util.Register(config.GidlFile{
		Filename: "byte_array.gen.gidl",
		Gen:      gidlGenByteArray,
		Benchmarks: []config.Benchmark{
			{
				Name:    "ByteArray/16",
				Comment: `16 byte array in a struct`,
				Config: config.Config{
					"size": 16,
				},
			},
			{
				Name:    "ByteArray/256",
				Comment: `256 byte array in a struct`,
				Config: config.Config{
					"size": 256,
				},
			},
			{
				Name: "ByteArray/4096",
				Comment: `
			4096 byte array in a struct
			Disabled on HLCPP / LLCPP due to clang performance issues`,
				Config: config.Config{
					"size": 4096,
				},
				Denylist: []config.Binding{config.HLCPP, config.LLCPP, config.Rust, config.Walker, config.Rust},
			},
		},
	})
}

func gidlGenByteArray(conf config.Config) (string, error) {
	size := conf.GetInt("size")

	return fmt.Sprintf(`
ByteArray%[1]d{
	bytes: [
%[2]s
	]
}`, size, util.List(size, util.SequentialHexValues(types.Uint8, 0))), nil
}
