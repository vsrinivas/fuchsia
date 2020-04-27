// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import "fmt"

var ByteArrayGidl = GidlFile{
	Filename: "byte_array.gidl",
	Gen:      GidlGenByteArray,
	Benchmarks: []Benchmark{
		{
			Name: "ByteArray/16",
			Config: Config{
				Size: 16,
			},
		},
		{
			Name: "ByteArray/256",
			Config: Config{
				Size: 256,
			},
		},
		{
			Name: "ByteArray/4096",
			Config: Config{
				Size: 4096,
			},
			Denylist: []Binding{HLCPP, LLCPP},
		},
		{
			Name: "ByteArray/65536",
			Config: Config{
				Size: 65536,
			},
			Denylist: []Binding{HLCPP, LLCPP},
		},
	},
}

func GidlGenByteArray(conf Config) (string, error) {
	size := conf[Size].(int)

	return fmt.Sprintf(`ByteArray%[1]d{
	bytes: [
%[2]s
	]
}`, size, gidlBytes(size)), nil
}
