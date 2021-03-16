// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"testing"

	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

func TestChangeIfReserved(t *testing.T) {
	assertEqual(t, changeIfReserved(fidl.Identifier("not_reserved")), "not_reserved")
	assertEqual(t, changeIfReserved(fidl.Identifier("foobar")), "foobar")

	// C++ keyword
	assertEqual(t, changeIfReserved(fidl.Identifier("switch")), "switch_")

	// Prevalent C constants
	assertEqual(t, changeIfReserved(fidl.Identifier("EPERM")), "EPERM_")

	// Bindings API
	assertEqual(t, changeIfReserved(fidl.Identifier("Unknown")), "Unknown_")
}
