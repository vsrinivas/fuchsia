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
		Filename: "gpu_magma.gen.gidl",
		Gen:      gidlGenGpuMagma,
		Benchmarks: []config.Benchmark{
			{
				Name: "GPUMagma/ExecuteImmediateCommandsRequest/CommandByte128/Semaphore8",
				Comment: `Based on fuchsia.gpu.magma.Primary.ExecuteImmediateCommand
			128 bytes was the most common command size in a trace`,
				Config: config.Config{
					"num_command_bytes": 128,
					"num_semaphores":    8,
				},
			},
			{
				Name: "GPUMagma/ExecuteImmediateCommandsRequest/CommandByte1024/Semaphore32",
				Comment: `Based on fuchsia.gpu.magma.Primary.ExecuteImmediateCommand
			1024 bytes command size appeared as a small cluster in a trace`,
				Config: config.Config{
					"num_command_bytes": 1024,
					"num_semaphores":    32,
				},
				// GIDL currently generates output that is slow to compile in clang.
				Denylist: []config.Binding{config.LLCPP, config.HLCPP, config.CPP},
			},
		},
	})
}

func gidlGenGpuMagma(conf config.Config) (string, error) {
	numCommandBytes := conf.GetInt("num_command_bytes")
	numSemaphores := conf.GetInt("num_semaphores")

	return fmt.Sprintf(`
ExecuteImmediateCommandsRequest{
	context_id: 1,
	command_data: [
		%[1]s
	],
	semaphores: [
		%[2]s
	],
}`,
		util.List(numCommandBytes, util.SequentialHexValues(types.Uint8, 0)),
		util.List(numSemaphores, util.SequentialValues(types.Uint64, 0))), nil
}
