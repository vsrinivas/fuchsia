// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package fidl

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/config"
	"go.fuchsia.dev/fuchsia/src/tests/benchmarks/fidl/benchmark_suite/gen/fidl/util"
)

func init() {
	util.Register(config.FidlFile{
		Filename:        "handle_array_event.gen.test.fidl",
		Gen:             fidlGenHandleArrayEvent,
		ExtraDefinition: `using zx;`,
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
					"size": 64,
				},
			},
		},
	})
}

func fidlGenHandleArrayEvent(config config.Config) (string, error) {
	size := config.GetInt("size")
	return fmt.Sprintf(`
type HandleArrayEvent%[1]d = resource struct{
	handles array<zx.handle:EVENT, %[1]d>;
};`, size), nil
}
