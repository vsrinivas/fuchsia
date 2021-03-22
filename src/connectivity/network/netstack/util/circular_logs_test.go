// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util_test

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/util"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
)

func TestCircularLogs(t *testing.T) {
	for _, test := range []struct {
		name           string
		capacity       int
		input          []util.LogEntry
		expectedOutput []util.LogEntry
	}{
		{
			name:           "empty",
			capacity:       3,
			input:          nil,
			expectedOutput: []util.LogEntry{},
		},
		{
			name:     "below capacity",
			capacity: 3,
			input: []util.LogEntry{
				{Content: "0"},
				{Content: "1"},
			},
			expectedOutput: []util.LogEntry{
				{Content: "0"},
				{Content: "1"},
			},
		},
		{
			name:     "at capacity",
			capacity: 3,
			input: []util.LogEntry{
				{Content: "0"},
				{Content: "1"},
				{Content: "2"},
			},
			expectedOutput: []util.LogEntry{
				{Content: "0"},
				{Content: "1"},
				{Content: "2"},
			},
		},
		{
			name:     "above capacity",
			capacity: 3,
			input: []util.LogEntry{
				{Content: "0"},
				{Content: "1"},
				{Content: "2"},
				{Content: "3"},
				{Content: "4"},
			},
			expectedOutput: []util.LogEntry{
				{Content: "2"},
				{Content: "3"},
				{Content: "4"},
			},
		},
	} {
		t.Run(test.name, func(t *testing.T) {
			c := util.MakeCircularLogs(test.capacity)
			for _, e := range test.input {
				c.Push(e.Content)
			}
			if diff := cmp.Diff(test.expectedOutput, c.BuildLogs(), cmpopts.IgnoreFields(
				util.LogEntry{},
				"Timestamp"),
			); diff != "" {
				t.Errorf("BuildLogs() mismatch (-want +got):\n%s", diff)
			}
		})
	}
}
