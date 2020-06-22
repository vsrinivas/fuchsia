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
		Filename: "union.gen.gidl",
		Gen:      gidlGenUnion,
		Benchmarks: []config.Benchmark{
			{
				Name:    "Union/LastSet/1",
				Comment: `Union with 1 possible tag that has 1st tag set`,
				Config: config.Config{
					"size": 1,
				},
				Denylist: []config.Binding{config.Dart},
			},
			{
				Name:    "Union/LastSet/16",
				Comment: `Union with 16 possible tags that has 16th tag set`,
				Config: config.Config{
					"size": 16,
				},
				Denylist: []config.Binding{config.Dart},
			},
			{
				Name:    "Union/LastSet/256",
				Comment: `Union with 256 possible tags that has 256th tag set`,
				Config: config.Config{
					"size": 256,
				},
				Denylist: []config.Binding{config.Dart},
			},
		},
	})
}

func gidlGenUnion(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	return fmt.Sprintf(`
Union%[1]dStruct{
	u: Union%[1]d{
		field%[1]d: 1,
	},
}`, size), nil
}
