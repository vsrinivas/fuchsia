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
		Filename: "byte_vector.gen.gidl",
		Gen:      gidlGenByteVector,
		Benchmarks: []config.Benchmark{
			{
				Name:    "ByteVector/16",
				Comment: `16 byte vector in a struct`,
				Config: config.Config{
					"size": 16,
				},
				EnableSendEventBenchmark: true,
				EnableEchoCallBenchmark:  true,
				Allowlist:                []config.Binding{config.LLCPP, config.DriverCPP, config.DriverLLCPP, config.HLCPP, config.CPP, config.Rust, config.Go, config.Walker, config.Reference, config.Dart},
			},
			{
				Name:    "ByteVector/256",
				Comment: `256 byte vector in a struct`,
				Config: config.Config{
					"size": 256,
				},
				EnableSendEventBenchmark: true,
				EnableEchoCallBenchmark:  true,
				Allowlist:                []config.Binding{config.LLCPP, config.DriverCPP, config.DriverLLCPP, config.HLCPP, config.CPP, config.Rust, config.Go, config.Walker, config.Reference, config.Dart},
			},
			{
				Name:    "ByteVector/4096",
				Comment: `4096 byte vector in a struct`,
				Config: config.Config{
					"size": 4096,
				},
				EnableSendEventBenchmark: true,
				EnableEchoCallBenchmark:  true,
				Allowlist:                []config.Binding{config.LLCPP, config.DriverCPP, config.DriverLLCPP, config.HLCPP, config.CPP, config.Rust, config.Go, config.Walker, config.Reference, config.Dart},
			},
			{
				Name: "ByteVector/65280",
				Comment: `65280 byte vector in a struct
			This needs to be at least 16 bytes less than 65536 to account for the buffer header.
			To make the values repeat in a full 256-element cycle, it needs to be 256 less than
			65536.
			Disabled on Rust / HLCPP due to clang performance issues`,
				Config: config.Config{
					"size": 65280,
				},
				Denylist: []config.Binding{config.Rust, config.HLCPP, config.Walker},
			},
		},
	})
}

func gidlGenByteVector(conf config.Config) (string, error) {
	size := conf.GetInt("size")

	return fmt.Sprintf(`
ByteVector{
	bytes: [
%[1]s
	]
}`, util.List(size, util.SequentialHexValues(types.Uint8, 0))), nil
}
