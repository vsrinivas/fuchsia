// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"io"
	"reflect"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
)

func TestRunTest(t *testing.T) {
	mockDataSinks := runtests.DataSinkMap{
		"sink": []runtests.DataSink{
			{
				Name: "foo",
				File: "/path/to/sink",
			},
		},
	}

	cases := []struct {
		name            string
		timeout         time.Duration
		tester          Tester
		expectResult    runtests.TestResult
		expectDataSinks runtests.DataSinkMap
	}{
		{
			name:    "timeout set on ctx",
			timeout: time.Second, // Arbitrary positive timeout
			tester: func(ctx context.Context, test build.Test, stdout, stderr io.Writer) (runtests.DataSinkMap, error) {
				if _, ok := ctx.Deadline(); !ok {
					return nil, fmt.Errorf("Expected ctx to have a deadline, but it does not")
				}
				return mockDataSinks, nil
			},
			expectResult:    runtests.TestSuccess,
			expectDataSinks: mockDataSinks,
		},
	}
	for _, tt := range cases {
		t.Run(tt.name, func(t *testing.T) {
			oldPerTestTimeout := perTestTimeout
			perTestTimeout = tt.timeout
			defer func() {
				perTestTimeout = oldPerTestTimeout
			}()
			result, err := runTest(context.Background(), build.Test{}, tt.tester)
			if err != nil {
				t.Errorf("Unexpected error: %v", err)
			}
			if result.Result != tt.expectResult {
				t.Errorf("Result = %v, want %v", result.Result, tt.expectResult)
			}
			if !reflect.DeepEqual(result.DataSinks, tt.expectDataSinks) {
				t.Errorf("Result = %v, want %v", result.DataSinks, tt.expectDataSinks)
			}
		})
	}
}
