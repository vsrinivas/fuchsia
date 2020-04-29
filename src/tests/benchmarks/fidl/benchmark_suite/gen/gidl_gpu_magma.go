// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import "fmt"

var GpuMagmaGidl = GidlFile{
	Filename: "gpu_magma.gidl",
	Gen:      GidlGenGpuMagma,
	Benchmarks: []Benchmark{
		{
			Name: "GPUMagma/ExecuteImmediateCommandsRequest/CommandByte128/Semaphore8",
			Config: Config{
				"num_command_bytes": 128,
				"num_semaphores": 8,
			},
		},
		{
			Name: "GPUMagma/ExecuteImmediateCommandsRequest/CommandByte1024/Semaphore32",
			Config: Config{
				"num_command_bytes": 1024,
				"num_semaphores": 32,
			},
			// GIDL currently generates output that is slow to compile in clang.
			Denylist: []Binding{LLCPP, HLCPP},
		},
	},
}

func GidlGenGpuMagma(conf Config) (string, error) {
	numCommandBytes := conf["num_command_bytes"].(int)
	numSemaphores := conf["num_semaphores"].(int)

	return fmt.Sprintf(`ExecuteImmediateCommandsRequest{
	context_id: 1,
	command_data: [
		%[1]s
	],
	semaphores: [
		%[2]s
	],
}`, gidlBytes(numCommandBytes), gidlBytes(numSemaphores)), nil
}
