// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type zxName = struct {
	typeName string
	prefix   string
}

var zirconNames = map[string]zxName{
	"rights": {
		typeName: "zx_rights_t",
		prefix:   "ZX_RIGHT",
	},
	"obj_type": {
		typeName: "zx_obj_type_t",
		prefix:   "ZX_OBJ_TYPE",
	},
}

func isZirconIdentifier(ci fidlgen.CompoundIdentifier) bool {
	return len(ci.Library) == 1 && ci.Library[0] == fidlgen.Identifier("zx")
}

func zirconName(ci fidlgen.CompoundIdentifier) name {
	if ci.Member != "" {
		if zn, ok := zirconValueMember(ci.Name, ci.Member); ok {
			return zn
		}
	} else {
		if zn, ok := zirconType(ci.Name); ok {
			return zn
		}
		if zn, ok := zirconConst(ci.Name); ok {
			return zn
		}
	}

	panic(fmt.Sprintf("Unknown zircon identifier: %s", ci.Encode()))
}

func zirconType(id fidlgen.Identifier) (name, bool) {
	n := string(id)
	if zn, ok := zirconNames[n]; ok {
		return makeName(zn.typeName), true
	}

	return name{}, false
}

func zirconValueMember(id fidlgen.Identifier, mem fidlgen.Identifier) (name, bool) {
	n := string(id)
	m := string(mem)
	if zn, ok := zirconNames[n]; ok {
		return makeName(fmt.Sprintf("%s_%s", zn.prefix, strings.ToUpper(m))), true
	}

	return name{}, false
}

func zirconConst(id fidlgen.Identifier) (name, bool) {
	n := string(id)
	if n == strings.ToUpper(n) {
		// All-caps names like `CHANNEL_MAX_MSG_BYTES`` get a ZX_ prefix.
		return makeName(fmt.Sprintf("ZX_%s", n)), true
	}

	return name{}, false
}
