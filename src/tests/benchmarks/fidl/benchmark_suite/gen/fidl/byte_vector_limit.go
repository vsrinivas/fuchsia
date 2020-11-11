// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package fidl

import (
	"fmt"
	"gen/config"
	"gen/fidl/util"
)

func init() {
	util.Register(config.FidlFile{
		Filename: "byte_vector_limit.gen.test.fidl",
		Gen:      fidlGenByteVectorLimit,
		Definitions: []config.Definition{
			{
				Config: config.Config{
					"limit": 1,
				},
			},
		},
	})
}

func fidlGenByteVectorLimit(config config.Config) (string, error) {
	limit := config.GetInt("limit")
	return fmt.Sprintf(`
struct ByteVectorLimit%[1]d {
	vector<uint8>:%[1]d bytes;
};`, limit), nil
}
