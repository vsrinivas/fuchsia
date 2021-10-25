// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package go_bindings_test

import (
	"testing"

	"fidl/fidl/go/types"
)

func TestEmptyFlexibleEnum(t *testing.T) {
	if !types.EmptyFlexibleEnum_Unknown.IsUnknown() {
		t.Fatalf("should be unknown")
	}
	if types.EmptyFlexibleEnum_Unknown.String() != "Unknown" {
		t.Fatalf("found %s", types.EmptyFlexibleEnum_Unknown.String())
	}
}
