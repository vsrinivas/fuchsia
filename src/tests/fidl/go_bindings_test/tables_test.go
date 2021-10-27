// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package go_bindings_test

import (
	"bytes"
	"testing"

	"syscall/zx/fidl"

	"fidl/fidl/go/types"
)

func TestTable(t *testing.T) {
	var emptyTable types.SimpleTable
	if emptyTable.HasUnknownData() {
		t.Errorf("Expected new table to have empty unknown data")
	}

	table := types.SimpleTable{
		I_unknownData: map[uint64]fidl.UnknownData{
			3: {
				Bytes: []byte{51},
			},
		},
	}

	table.SetX(5)
	if !table.HasX() {
		t.Errorf("Expected X to be set")
	}
	if table.GetX() != 5 {
		t.Errorf("Expected X value 5, got: %d", table.GetX())
	}
	if table.HasY() {
		t.Errorf("Expected Y to be unset")
	}
	y := table.GetYWithDefault(13)
	if y != 13 {
		t.Errorf("Expected default Y value 13, got: %d", y)
	}
	if !table.HasUnknownData() {
		t.Errorf("Expected table to have unknown data")
	}
	unknownData := table.GetUnknownData()
	data, ok := unknownData[3]
	if !ok {
		t.Errorf("Expected table to have unknown ordinal 3")
	}
	if want := []byte{51}; !bytes.Equal(data.Bytes, want) {
		t.Errorf("Expected unknown data to be %s, got: %s", want, data.Bytes)
	}
	if len(data.Handles) != 0 {
		t.Errorf("Expected empty unknown handles")
	}
}
