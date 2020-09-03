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
		Filename: "handle_array_event.gen.gidl",
		Gen:      gidlGenHandleArrayEvent,
		Benchmarks: []config.Benchmark{
			{
				Name:    "HandleArray/Event/64",
				Comment: `64 event handle array in a struct`,
				Config: config.Config{
					"size": 64,
				},
				EnableSendEventBenchmark: true,
				EnableEchoCallBenchmark:  true,
				HandleDefs:               util.RepeatHandleDef(config.HandleDef{Subtype: config.Event}, 64),
				Allowlist:                []config.Binding{config.Rust},
			},
		},
	})
}

func gidlGenHandleArrayEvent(conf config.Config) (string, error) {
	size := conf.GetInt("size")
	handleValues := ""
	for i := 0; i < size; i++ {
		handleValues += fmt.Sprintf("#%d,\n", i)
	}
	return fmt.Sprintf(`
HandleArrayEvent%[1]d{
	handles: [
%[2]s
	]
}`, size, handleValues), nil
}
