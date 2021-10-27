// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package go_bindings_test

import (
	"fmt"
	"testing"

	"fidl/fidl/go/types"
)

type stringTestCase struct {
	val      fmt.Stringer
	expected string
}

func (ex *stringTestCase) check(t *testing.T) {
	if actual := ex.val.String(); actual != ex.expected {
		t.Errorf("%+v: actual='%s', expected='%s'", ex.val, actual, ex.expected)
	}
}

func TestEnumString(t *testing.T) {
	cases := []stringTestCase{
		{types.StrictEnumMemberA, "MemberA"},
		{types.StrictEnumMemberB, "MemberB"},
		{types.StrictEnumMemberC, "MemberC"},

		{types.FlexibleEnumMemberA, "MemberA"},
		{types.FlexibleEnumMemberB, "MemberB"},
		{types.FlexibleEnumMemberC, "MemberC"},
		{types.FlexibleEnumMemberCustomUnknown, "MemberCustomUnknown"},
		{types.FlexibleEnum_Unknown, "MemberCustomUnknown"},

		{types.EmptyFlexibleEnum_Unknown, "Unknown"},
	}
	for _, ex := range cases {
		ex.check(t)
	}
}

func TestBitsString(t *testing.T) {
	cases := []stringTestCase{
		{types.StrictBits(0), "<empty bits>"},
		{types.StrictBitsMemberA, "MemberA"},
		{types.StrictBitsMemberB, "MemberB"},
		{types.StrictBitsMemberC, "MemberC"},
		{types.StrictBitsMemberA | types.StrictBitsMemberC, "MemberA|MemberC"},

		{types.FlexibleBits(0), "<empty bits>"},
		{types.FlexibleBitsMemberA, "MemberA"},
		{types.FlexibleBitsMemberB, "MemberB"},
		{types.FlexibleBitsMemberC, "MemberC"},
		{types.FlexibleBitsMemberA | types.FlexibleBitsMemberC, "MemberA|MemberC"},
	}
	for _, ex := range cases {
		ex.check(t)
	}
}
