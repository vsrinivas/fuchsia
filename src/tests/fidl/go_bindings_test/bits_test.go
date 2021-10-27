// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package go_bindings_test

import (
	"testing"

	"fidl/fidl/go/types"
)

func TestBitsApi(t *testing.T) {
	var knownBits, unknownBits uint64 = 0b101, 0b110000
	unknownStrict := types.StrictBits(knownBits | unknownBits)
	if !unknownStrict.HasUnknownBits() {
		t.Error("unknown bits returned false for HasUnknownBits")
	}
	if unknownStrict.GetUnknownBits() != unknownBits {
		t.Errorf(
			"Got wrong unknown bits: expected %b, was %b", unknownBits, unknownStrict.GetUnknownBits())
	}
	if unknownStrict.InvertBits() != 0b10 {
		t.Errorf(
			"Got wrong InvertBits: expected %b, got %b", 0b10, unknownStrict.InvertBits())
	}
	if unknownStrict.InvertBits().InvertBits() != types.StrictBits(knownBits) {
		t.Errorf(
			"Got wrong double InvertBits: expected %b, got %b", knownBits, unknownStrict.InvertBits().InvertBits())
	}
	if !unknownStrict.HasBits(0b1) {
		t.Errorf("Failed to match on known bits for HasBits")
	}
	if !unknownStrict.HasBits(0b110000) {
		t.Errorf("Failed to match on unknown bits for HasBits")
	}
	if unknownStrict.HasBits(0b10) {
		t.Errorf("Matched on known bits for HasBits when it should not")
	}
	if unknownStrict.HasBits(0b1000) {
		t.Errorf("Matched on unknown bits for HasBits when it should not")
	}
	if unknownStrict.ClearBits(0b1) != types.StrictBits(0b110100) {
		t.Errorf("Failed to clear known bits for ClearBits")
	}
	if unknownStrict.ClearBits(0b10000) != types.StrictBits(0b100101) {
		t.Errorf("Failed to clear unknown bits for ClearBits")
	}
	if unknownStrict != unknownStrict.ClearBits(0b10) {
		t.Errorf("Cleared known bits when it should not")
	}
	if unknownStrict != unknownStrict.ClearBits(0b1000) {
		t.Errorf("Cleared unknown bits when it should not")
	}

	knownStrict := unknownStrict & types.StrictBits_Mask
	if expected := types.StrictBitsMemberA | types.StrictBitsMemberC; knownStrict != expected {
		t.Errorf("Expected masked value: %b, got %b", expected, knownStrict)
	}
}

func TestFlexibleBitsApi(t *testing.T) {
	var knownBits, unknownBits uint64 = 0b110000, 0b101
	unknownFlexible := types.FlexibleBits(knownBits | unknownBits)
	if !unknownFlexible.HasUnknownBits() {
		t.Error("unknown bits returned false for HasUnknownBits")
	}
	if unknownFlexible.GetUnknownBits() != unknownBits {
		t.Errorf(
			"Got wrong unknown bits: expected %b, was %b", unknownBits, unknownFlexible.GetUnknownBits())
	}
	if unknownFlexible.InvertBits() != 0b001000 {
		t.Errorf(
			"Got wrong InvertBits: expected %b, got %b", 0b001000, unknownFlexible.InvertBits())
	}
	if unknownFlexible.InvertBits().InvertBits() != types.FlexibleBits(knownBits) {
		t.Errorf(
			"Got wrong double InvertBits: expected %b, got %b", knownBits, unknownFlexible.InvertBits().InvertBits())
	}
	if !unknownFlexible.HasBits(0b1) {
		t.Errorf("Failed to match on known bits for HasBits")
	}
	if !unknownFlexible.HasBits(0b110000) {
		t.Errorf("Failed to match on unknown bits for HasBits")
	}
	if unknownFlexible.HasBits(0b10) {
		t.Errorf("Matched on known bits for HasBits when it should not")
	}
	if unknownFlexible.HasBits(0b1000) {
		t.Errorf("Matched on unknown bits for HasBits when it should not")
	}
	if unknownFlexible.ClearBits(0b1) != types.FlexibleBits(0b110100) {
		t.Errorf("Failed to clear known bits for ClearBits")
	}
	if unknownFlexible.ClearBits(0b10000) != types.FlexibleBits(0b100101) {
		t.Errorf("Failed to clear unknown bits for ClearBits")
	}
	if unknownFlexible != unknownFlexible.ClearBits(0b10) {
		t.Errorf("Cleared known bits when it should not")
	}
	if unknownFlexible != unknownFlexible.ClearBits(0b1000) {
		t.Errorf("Cleared unknown bits when it should not")
	}

	knownFlexible := unknownFlexible & types.FlexibleBits_Mask
	if expected := types.FlexibleBitsMemberB | types.FlexibleBitsMemberC; knownFlexible != expected {
		t.Errorf("Expected masked value: %b, got %b", expected, knownFlexible)
	}
}

func TestBitsInvertBits(t *testing.T) {
	cases := []struct {
		left, right types.StrictBits
	}{
		{types.StrictBits(0), types.StrictBitsMemberA | types.StrictBitsMemberB | types.StrictBitsMemberC},
		{types.StrictBitsMemberA, types.StrictBitsMemberB | types.StrictBitsMemberC},
		{types.StrictBitsMemberB, types.StrictBitsMemberA | types.StrictBitsMemberC},
		{types.StrictBitsMemberC, types.StrictBitsMemberA | types.StrictBitsMemberB},
	}
	for _, ex := range cases {
		if ex.left.InvertBits() != ex.right {
			t.Errorf("%s should invert to %s", ex.left, ex.right)
		}
		if ex.right.InvertBits() != ex.left {
			t.Errorf("%s should invert to %s", ex.right, ex.left)
		}
	}
}

func TestBitsHasBits(t *testing.T) {
	all := []types.StrictBits{
		types.StrictBitsMemberA,
		types.StrictBitsMemberB,
		types.StrictBitsMemberC,
	}
	cases := []struct {
		val types.StrictBits
		has []types.StrictBits
	}{
		{types.StrictBits(0), nil},
		{types.StrictBitsMemberA, []types.StrictBits{
			types.StrictBitsMemberA,
		}},
		{types.StrictBitsMemberA | types.StrictBitsMemberC, []types.StrictBits{
			types.StrictBitsMemberA,
			types.StrictBitsMemberC,
		}},
	}
	for _, ex := range cases {
		hasOrHasNot := make(map[types.StrictBits]bool)
		for _, b := range ex.has {
			hasOrHasNot[b] = true
		}
		for _, b := range all {
			if hasOrHasNot[b] {
				if !ex.val.HasBits(b) {
					t.Errorf("%s should have bits %s", ex.val, b)
				}
			} else {
				if ex.val.HasBits(b) {
					t.Errorf("%s should not have bits %s", ex.val, b)
				}
			}
		}
	}
}
