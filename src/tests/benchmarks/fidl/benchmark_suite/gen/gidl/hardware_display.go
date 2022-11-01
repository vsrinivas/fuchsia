// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package gidl

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/config"
	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/gidl/util"
	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/types"
)

func init() {
	util.Register(config.GidlFile{
		Filename: "hardware_display.gen.gidl",
		Gen:      gidlGenHardwareDisplay,
		Benchmarks: []config.Benchmark{
			{
				Name:    "HardwareDisplay/OnVsyncEvent/Image64",
				Comment: `Based on fuchsia.hardware.display.Controller.OnVsync`,
				Config: config.Config{
					"num_images": 32,
				},
				Denylist: []config.Binding{config.Rust},
			},
		},
	})
}

func gidlGenHardwareDisplay(conf config.Config) (string, error) {
	numImages := conf.GetInt("num_images")

	return fmt.Sprintf(`
OnVsyncEvent{
	display_id: 1,
	timestamp: 1,
	images: [
		%[1]s
	],
}`, util.List(numImages, util.SequentialValues(types.Uint8, 0))), nil
}
