// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gidl

import (
	"fmt"
	"gen/config"
	"gen/gidl/util"
)

func init() {
	util.Register(config.GidlFile{
		Filename: "table_all_unset.gen.gidl",
		Gen:      gidlGenTableAllUnset,
		Benchmarks: []config.Benchmark{
			{
				Name:    "Table/Unset/1",
				Comment: `Table with 1 field all unset`,
				Config: config.Config{
					"size": 1,
				},
			},
			{
				Name:    "Table/Unset/16",
				Comment: `Table with 16 fields all unset`,
				Config: config.Config{
					"size": 16,
				},
			},
			{
				Name:    "Table/Unset/256",
				Comment: `Table with 256 fields all unset`,
				Config: config.Config{
					"size": 256,
				},
				// Dart has a 256 argument limit which is exceeded by the table constructor.
				Denylist: []config.Binding{config.Dart},
			},
		},
	})
}

func gidlGenTableAllUnset(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	return fmt.Sprintf(`
Table%[1]dStruct{
	value: Table%[1]d{},
}`, size), nil
}
