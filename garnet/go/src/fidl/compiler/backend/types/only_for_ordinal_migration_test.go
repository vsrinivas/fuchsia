// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package types

import (
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestMethodOrdinalsFormatsToHex(t *testing.T) {
	expected := "0xccc3b0b230"
	actual := fmt.Sprintf("%v", methodOrdinal(879456465456))
	if actual != expected {
		t.Errorf("actual=%v, expected=%v", actual, expected)
	}
}

type testCase struct {
	name          string
	input         Ordinals
	expectedReads []string
	expectedWrite string
}

func (ex testCase) run(t *testing.T) {
	t.Run(ex.name, func(t *testing.T) {
		actualReads := names(ex.input.Reads())
		actualWrite := ex.input.Write().Name
		if diff := cmp.Diff(ex.expectedReads, actualReads); diff != "" {
			t.Errorf("%v) reads: expected != actual (-want +got)\n%s", ex, diff)
		}
		if diff := cmp.Diff(ex.expectedWrite, actualWrite); diff != "" {
			t.Errorf("%v) write: expected != actual (-want +got)\n%s", ex, diff)
		}
	})
}

func TestStep3(t *testing.T) {
	// At step 3, fidlc emits 32b, but shifts gen << 32.
	cases := []testCase{
		// Both ordinals are the same, use ord (gen is << 32).
		{
			name: "ord_and_gen_same",
			input: NewOrdinalsStep3(Method{
				Ordinal:    0x7676e0ea,
				GenOrdinal: 0x7676e0ea00000000,
			}, "ord", "gen"),
			expectedReads: []string{"gen"},
			expectedWrite: "gen",
		},
		// Both ordinals are different, use both on reads, ord on write.
		{
			name: "ord_and_gen_different",
			input: NewOrdinalsStep3(Method{
				Ordinal:    0x7676e0ea,
				GenOrdinal: 0xae0e676700000000,
			}, "ord", "gen"),
			expectedReads: []string{"ord", "gen"},
			expectedWrite: "gen",
		},
		// Assume ord is already shifted (by fidlc), it results in a 0 ordinal
		// which should be ignored.
		{
			name: "ord_should_be_0_and_ignored",
			input: NewOrdinalsStep3(Method{
				Ordinal:    0x7676e0ea00000000,
				GenOrdinal: 0x7676e0ea00000000,
			}, "ord", "gen"),
			expectedReads: []string{"gen"},
			expectedWrite: "gen",
		},
	}
	for _, ex := range cases {
		ex.run(t)
	}
}

func TestStep5(t *testing.T) {
	// At step 5, fidlc emits 64b and bindings should just use them.
	cases := []testCase{
		{
			name: "ord_and_gen_same",
			input: NewOrdinalsStep5(Method{
				Ordinal:    0x7676e0ea00000000,
				GenOrdinal: 0x7676e0ea00000000,
			}, "ord", "gen"),
			expectedReads: []string{"ord"},
			expectedWrite: "ord",
		},
		// Both ordinals are different, use both on reads, ord on write.
		{
			name: "ord_and_gen_different",
			input: NewOrdinalsStep5(Method{
				Ordinal:    0x7676e0ea00000000,
				GenOrdinal: 0xae0e676700000000,
			}, "ord", "gen"),
			expectedReads: []string{"ord", "gen"},
			expectedWrite: "ord",
		},
	}
	for _, ex := range cases {
		ex.run(t)
	}
}

func TestStep7(t *testing.T) {
	// At step 7, fidlc emits 64b and bindings should just use them, send gen.
	cases := []testCase{
		{
			name: "ord_and_gen_same",
			input: NewOrdinalsStep7(Method{
				Ordinal:    0x7676e0ea00000000,
				GenOrdinal: 0x7676e0ea00000000,
			}, "ord", "gen"),
			expectedReads: []string{"gen"},
			expectedWrite: "gen",
		},
		// Both ordinals are different, use both on reads, ord on write.
		{
			name: "ord_and_gen_different",
			input: NewOrdinalsStep7(Method{
				Ordinal:    0x7676e0ea00000000,
				GenOrdinal: 0xae0e676700000000,
			}, "ord", "gen"),
			expectedReads: []string{"ord", "gen"},
			expectedWrite: "gen",
		},
	}
	for _, ex := range cases {
		ex.run(t)
	}
}

func names(namedOrdinals []NamedOrdinal) []string {
	var names []string
	for _, namedOrdinal := range namedOrdinals {
		names = append(names, namedOrdinal.Name)
	}
	return names
}
