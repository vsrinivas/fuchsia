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
				Denylist: []config.Binding{config.Rust},
			},
		},
	})
}

func fidlGenUnion(config config.Config) (string, error) {
	size := config.GetInt("size")
	denylist := ""
	if size == 256 {
		denylist = `[BindingsDenylist = "rust"]`
	}
	return fmt.Sprintf(`
struct Union%[1]dStruct {
	Union%[1]d u;
};

%[3]s
union Union%[1]d {
	%[2]s
};`, size, util.OrdinalFields(types.Uint8, "field", size), denylist), nil
}
