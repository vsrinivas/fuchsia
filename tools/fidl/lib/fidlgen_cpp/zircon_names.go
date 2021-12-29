// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

var zirconNames = map[string]string{
	"rights":   "zx_rights_t",
	"obj_type": "zx_obj_type_t",
}

func isZirconIdentifier(ci fidlgen.CompoundIdentifier) bool {
	return len(ci.Library) == 1 && ci.Library[0] == fidlgen.Identifier("zx")
}

func zirconName(ci fidlgen.CompoundIdentifier) name {
	name := string(ci.Name)
	if ci.Member == "" {
		n, ok := zirconNames[name]
		if ok {
			return makeName(n)
		}

		if name == strings.ToUpper(name) {
			// all-caps names get a ZX_ prefix
			return makeName(fmt.Sprintf("ZX_%s", name))
		}
	}

	panic(fmt.Sprintf("Unknown zircon identifier: %s", ci.Encode()))
}
