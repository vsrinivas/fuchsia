// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util_test

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"

	"github.com/google/go-cmp/cmp"
)

func TestCircularLogs(t *testing.T) {
	for _, test := range []struct {
		name             string
		capacity         int
		ignoreDuplicates bool
		input            []util.LogEntry
		expectedOutput   []util.LogEntry
	}{
		{
			name:             "empty",
			capacity:         3,
			ignoreDuplicates: false,
			input:            nil,
			expectedOutput:   []util.LogEntry{},
		},
		{
			name:             "below capacity",
			capacity:         3,
			ignoreDuplicates: false,
			input: []util.LogEntry{
				{Timestamp: 0, Content: "0"},
				{Timestamp: 1, Content: "1"},
			},
			expectedOutput: []util.LogEntry{
				{Timestamp: 0, Content: "0"},
				{Timestamp: 1, Content: "1"},
			},
		},
		{
			name:             "at capacity",
			capacity:         3,
			ignoreDuplicates: false,
			input: []util.LogEntry{
				{Timestamp: 0, Content: "0"},
				{Timestamp: 1, Content: "1"},
				{Timestamp: 2, Content: "2"},
			},
			expectedOutput: []util.LogEntry{
				{Timestamp: 0, Content: "0"},
				{Timestamp: 1, Content: "1"},
				{Timestamp: 2, Content: "2"},
			},
		},
		{
			name:             "above capacity",
			capacity:         3,
			ignoreDuplicates: false,
			input: []util.LogEntry{
				{Timestamp: 0, Content: "0"},
				{Timestamp: 1, Content: "1"},
				{Timestamp: 2, Content: "2"},
				{Timestamp: 3, Content: "3"},
				{Timestamp: 4, Content: "4"},
			},
			expectedOutput: []util.LogEntry{
				{Timestamp: 2, Content: "2"},
				{Timestamp: 3, Content: "3"},
				{Timestamp: 4, Content: "4"},
			},
		},
		{
			name:             "ignore duplicates",
			capacity:         4,
			ignoreDuplicates: true,
			input: []util.LogEntry{
				{Timestamp: 0, Content: "0"},
				{Timestamp: 1, Content: "1"},
				{Timestamp: 2, Content: "1"},
				{Timestamp: 3, Content: "2"},
			},
			expectedOutput: []util.LogEntry{
				{Timestamp: 0, Content: "0"},
				{Timestamp: 1, Content: "1"},
				{Timestamp: 3, Content: "2"},
			},
		},
		{
			name:             "ignore duplicates above capacity",
			capacity:         3,
			ignoreDuplicates: true,
			input: []util.LogEntry{
				{Timestamp: 0, Content: "0"},
				{Timestamp: 1, Content: "1"},
				{Timestamp: 2, Content: "2"},
				{Timestamp: 3, Content: "3"},
				{Timestamp: 4, Content: "3"},
				{Timestamp: 5, Content: "4"},
			},
			expectedOutput: []util.LogEntry{
				{Timestamp: 2, Content: "2"},
				{Timestamp: 3, Content: "3"},
				{Timestamp: 5, Content: "4"},
			},
		},
	} {
		t.Run(test.name, func(t *testing.T) {
			c := util.MakeCircularLogs(test.capacity, test.ignoreDuplicates)
			for _, e := range test.input {
				c.Push(util.LogEntry{e.Timestamp, e.Content})
			}
			if diff := cmp.Diff(test.expectedOutput, c.BuildLogs()); diff != "" {
				t.Errorf("BuildLogs() mismatch (-want +got):\n%s", diff)
			}
		})
	}
}
