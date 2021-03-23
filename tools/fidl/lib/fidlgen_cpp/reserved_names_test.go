// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"testing"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

func TestChangeIfReserved(t *testing.T) {
	assertEqual(t, changeIfReserved(fidlgen.Identifier("not_reserved")), "not_reserved")
	assertEqual(t, changeIfReserved(fidlgen.Identifier("foobar")), "foobar")

	// C++ keyword
	assertEqual(t, changeIfReserved(fidlgen.Identifier("switch")), "switch_")

	// Prevalent C constants
	assertEqual(t, changeIfReserved(fidlgen.Identifier("EPERM")), "EPERM_")

	// Bindings API
	assertEqual(t, changeIfReserved(fidlgen.Identifier("Unknown")), "Unknown_")
}
