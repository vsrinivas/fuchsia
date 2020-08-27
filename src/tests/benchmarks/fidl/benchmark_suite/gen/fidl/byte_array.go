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
		Filename: "byte_array.gen.test.fidl",
		Gen:      fidlGenByteArray,
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

func fidlGenByteArray(config config.Config) (string, error) {
	size := config.GetInt("size")
	return fmt.Sprintf(`
struct ByteArray%[1]d {
	array<uint8>:%[1]d bytes;
};`, size), nil
}
