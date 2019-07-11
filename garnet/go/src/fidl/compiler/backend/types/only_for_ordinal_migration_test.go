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

func TestOrdinalsAtVariousMigrationPoints(t *testing.T) {
	cases := []struct {
		input         Ordinals
		expectedReads []string
		expectedWrite string
	}{
		// Both ordinals are the same, use ord.
		{
			input: NewOrdinals(Method{
				Ordinal:    0x7676e0ea,
				GenOrdinal: 0x7676e0ea,
			}, "ord", "gen"),
			expectedReads: []string{"ord"},
			expectedWrite: "ord",
		},
		// Both ordinals are different, use both on reads, ord on write.
		{
			input: NewOrdinals(Method{
				Ordinal:    0x7676e0ea,
				GenOrdinal: 0xae0e6767,
			}, "ord", "gen"),
			expectedReads: []string{"ord", "gen"},
			expectedWrite: "ord",
		},
		// Assume gen is already shifted (by fidlc), it results in a 0 ordinal
		// which should be ignored.
		{
			input: NewOrdinals(Method{
				Ordinal:    0x7676e0ea,
				GenOrdinal: 0x7676e0ea00000000,
			}, "ord", "gen"),
			expectedReads: []string{"ord"},
			expectedWrite: "ord",
		},
	}
	for _, ex := range cases {
		actualReads := names(ex.input.Reads())
		actualWrite := ex.input.Write().Name
		if diff := cmp.Diff(ex.expectedReads, actualReads); diff != "" {
			t.Errorf("%v) reads: expected != actual (-want +got)\n%s", ex, diff)
		}
		if diff := cmp.Diff(ex.expectedWrite, actualWrite); diff != "" {
			t.Errorf("%v) write: expected != actual (-want +got)\n%s", ex, diff)
		}
	}
}

func names(namedOrdinals []NamedOrdinal) []string {
	var names []string
	for _, namedOrdinal := range namedOrdinals {
		names = append(names, namedOrdinal.Name)
	}
	return names
}
