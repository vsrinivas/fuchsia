// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidl

import (
	"fmt"
	"gen/config"
	"gen/fidl/util"
)

func init() {
	util.Register(config.FidlFile{
		Filename: "handle_array.gen.test.fidl",
		Gen:      fidlGenHandleArray,
		Definitions: []config.Definition{
			{
				Config: config.Config{
					"size": 64,
				},
			},
		},
	})
}

func fidlGenHandleArray(config config.Config) (string, error) {
	size := config.GetInt("size")
	return fmt.Sprintf(`
struct HandleArray%[1]d {
	array<handle>:%[1]d handles;
};`, size), nil
}
