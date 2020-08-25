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
		Filename: "handle_array.gen.gidl",
		Gen:      gidlGenHandleArray,
		Benchmarks: []config.Benchmark{
			{
				Name:    "HandleArray/64",
				Comment: `64 handle array in a struct`,
				Config: config.Config{
					"size": 64,
				},
				HandleDefs: util.RepeatHandleDef(config.HandleDef{Subtype: config.Event}, 64),
				Allowlist:  []config.Binding{config.Rust},
			},
		},
	})
}

func gidlGenHandleArray(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	handleValues := ""
	for i := 0; i < size; i++ {
		handleValues += fmt.Sprintf("#%d,\n", i)
	}
	return fmt.Sprintf(`
HandleArray%[1]d{
	handles: [
%[2]s
	]
}`, size, handleValues), nil
}
