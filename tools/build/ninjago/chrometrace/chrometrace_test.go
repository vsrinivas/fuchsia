// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package chrometrace

import (
	"sort"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestSortByStart(t *testing.T) {
	for _, tc := range []struct {
		name  string
		input ByStart
		want  ByStart
	}{
		{
			name: "empty",
		},
		{
			name: "single element",
			input: ByStart([]Trace{
				{TimestampMicros: 1},
			}),
			want: ByStart([]Trace{
				{TimestampMicros: 1},
			}),
		},
		{
			name: "multiple elements",
			input: ByStart([]Trace{
				{TimestampMicros: 2},
				{TimestampMicros: 3},
				{TimestampMicros: 1},
			}),
			want: ByStart([]Trace{
				{TimestampMicros: 1},
				{TimestampMicros: 2},
				{TimestampMicros: 3},
			}),
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			got := tc.input
			sort.Sort(got)
			if diff := cmp.Diff(tc.want, got); diff != "" {
				t.Errorf("Got result diff after sort (-want +got):\n%s", diff)
			}
		})
	}
}
