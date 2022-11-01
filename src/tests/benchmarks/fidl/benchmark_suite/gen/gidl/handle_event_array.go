// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package gidl

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/config"
	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/gidl/util"
)

func init() {
	util.Register(config.GidlFile{
		Filename: "handle_array_event.gen.gidl",
		Gen:      gidlGenHandleArrayEvent,
		Benchmarks: []config.Benchmark{
			{
				Name:    "HandleArray/Event/1",
				Comment: `1 event handle array in a struct`,
				Config: config.Config{
					"size": 1,
				},
				HandleDefs: util.RepeatHandleDef(config.HandleDef{Subtype: config.Event}, 1),
				Denylist:   []config.Binding{config.Go},
			},
			{
				Name:    "HandleArray/Event/16",
				Comment: `16 event handle array in a struct`,
				Config: config.Config{
					"size": 16,
				},
				HandleDefs: util.RepeatHandleDef(config.HandleDef{Subtype: config.Event}, 16),
				Denylist:   []config.Binding{config.Go},
			},
			{
				Name:    "HandleArray/Event/64",
				Comment: `64 event handle array in a struct`,
				Config: config.Config{
					"size": 64,
				},
				EnableSendEventBenchmark: true,
				EnableEchoCallBenchmark:  true,
				HandleDefs:               util.RepeatHandleDef(config.HandleDef{Subtype: config.Event}, 64),
				Denylist:                 []config.Binding{config.Go},
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
