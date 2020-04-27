// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import "fmt"

var ByteArrayFidl = FidlFile{
	Filename: "byte_array.test.fidl",
	Gen:      FidlGenByteArray,
	Definitions: []Definition{
		{
			Config: Config{
				Size: 16,
			},
		},
		{
			Config: Config{
				Size: 256,
			},
		},
		{
			Config: Config{
				Size: 4096,
			},
		},
		{
			Config: Config{
				Size: 65536,
			},
		},
	},
}

func FidlGenByteArray(config Config) (string, error) {
	size := config[Size].(int)
	return fmt.Sprintf(`struct ByteArray%[1]d {
	array<uint8>:%[1]d bytes;
};`, size), nil
}
