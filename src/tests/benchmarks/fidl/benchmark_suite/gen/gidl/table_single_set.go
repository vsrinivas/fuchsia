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
		Filename: "table_single_set.gen.gidl",
		Gen:      gidlGenTableSingleSet,
		Benchmarks: []config.Benchmark{
			{
				Name:    "Table/SingleSet/1_of_1",
				Comment: `Table with 1 field with the 1st field set`,
				Config: config.Config{
					"size":         1,
					"field_to_set": 1,
				},
			},
			{
				Name:    "Table/SingleSet/1_of_16",
				Comment: `Table with 16 fields with the 1st field set`,
				Config: config.Config{
					"size":         16,
					"field_to_set": 1,
				},
			},
			{
				Name:    "Table/SingleSet/1_of_256",
				Comment: `Table with 256 fields with the 1st field set`,
				Config: config.Config{
					"size":         256,
					"field_to_set": 1,
				},
				// Dart has a 256 argument limit which is exceeded by the table constructor.
				Denylist: []config.Binding{config.Dart},
			},
			{
				Name:    "Table/SingleSet/16_of_16",
				Comment: `Table with 16 fields with the 16th field set`,
				Config: config.Config{
					"size":         16,
					"field_to_set": 16,
				},
			},
			{
				Name:    "Table/SingleSet/16_of_256",
				Comment: `Table with 256 fields with the 16th field set`,
				Config: config.Config{
					"size":         256,
					"field_to_set": 16,
				},
				// Dart has a 256 argument limit which is exceeded by the table constructor.
				Denylist: []config.Binding{config.Dart},
			},
			{
				Name:    "Table/SingleSet/256_of_256",
				Comment: `Table with 256 fields with the 256th field set`,
				Config: config.Config{
					"size":         256,
					"field_to_set": 256,
				},
				// Dart has a 256 argument limit which is exceeded by the table constructor.
				Denylist: []config.Binding{config.Dart},
			},
		},
	})
}

func gidlGenTableSingleSet(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	fieldToSet := conf.GetInt("field_to_set")
	return fmt.Sprintf(`
Table%[1]dStruct{
	value: Table%[1]d{
		field%[2]d: 1,
	},
}`, size, fieldToSet), nil
}
