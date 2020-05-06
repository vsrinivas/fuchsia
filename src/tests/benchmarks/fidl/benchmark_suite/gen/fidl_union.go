// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import "fmt"

var UnionFidl = FidlFile{
	Filename: "union.test.fidl",
	Gen:      FidlGenUnion,
	Definitions: []Definition{
		{
			Config: Config{
				Size: 1,
			},
		},
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
	},
}

func FidlGenUnion(config Config) (string, error) {
	size := config[Size].(int)
	return fmt.Sprintf(
		`struct Union%[1]dStruct {
	Union%[1]d u;
};

union Union%[1]d {
	%[2]s
};`, size, fidlOrdinalFields(uint8Type, "field", size)), nil
}
