// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
)

var UnionGidl = GidlFile{
	Filename: "union.gidl",
	Gen:      GidlGenUnion,
	Benchmarks: []Benchmark{
		{
			Name: "Union/LastSet/1",
			Config: Config{
				Size: 1,
			},
		},
		{
			Name: "Union/LastSet/16",
			Config: Config{
				Size: 16,
			},
		},
		{
			Name: "Union/LastSet/256",
			Config: Config{
				Size: 256,
			},
		},
	},
}

func GidlGenUnion(conf Config) (string, error) {
	size := conf[Size].(int)
	return fmt.Sprintf(
		`Union%[1]dStruct{
	u: Union%[1]d{
		field%[1]d: 1,
	},
}`, size), nil
}
