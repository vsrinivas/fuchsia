// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidl

import (
	"fmt"
	"gen/config"
	"gen/fidl/util"
	"gen/types"
)

func init() {
	util.Register(config.FidlFile{
		Filename: "table.gen.test.fidl",
		Gen:      fidlGenTable,
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
				// Dart has a 256 argument limit and all set table fields are passed into
				// the constructor.
				Denylist: []config.Binding{config.Dart, config.Rust},
			},
		},
	})
}

func fidlGenTable(config config.Config) (string, error) {
	size := config.GetInt("size")
	// TODO(fxb/57224) Make it possible to apply denylists to multiple types.
	denylist := ""
	if size == 256 {
		denylist = `[BindingsDenylist = "rust"]`
	}
	return fmt.Sprintf(`
struct Table%[1]dStruct {
	Table%[1]d value;
};

%[3]s
table Table%[1]d {
	%[2]s
};`, size, util.OrdinalFields(types.Uint8, "field", size), denylist), nil
}
