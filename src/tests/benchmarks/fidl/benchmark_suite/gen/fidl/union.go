// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package fidl

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/config"
	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/fidl/util"
	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/types"
)

func init() {
	util.Register(config.FidlFile{
		Filename: "union.gen.test.fidl",
		Gen:      fidlGenUnion,
		Definitions: []config.Definition{
			{
				Config: config.Config{
					"size": 1,
				},
			},
			{
				Config: config.Config{
					"size": 16,
				},
			},
			{
				Config: config.Config{
					"size": 256,
				},
			},
		},
	})
}

func fidlGenUnion(config config.Config) (string, error) {
	size := config.GetInt("size")
	return fmt.Sprintf(`
type Union%[1]dStruct = struct{
	u Union%[1]d;
};

type Union%[1]d = union{
	%[2]s
};`, size, util.OrdinalFields(types.Uint8, "field", size)), nil
}
