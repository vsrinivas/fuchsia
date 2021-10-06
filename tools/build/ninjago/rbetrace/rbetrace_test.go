// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rbetrace

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/chrometrace"
)

func TestFromRPLFile(t *testing.T) {
	origRunRPL2TraceCommand := runRPL2TraceCommand
	defer func() { runRPL2TraceCommand = origRunRPL2TraceCommand }()

	for _, tc := range []struct {
		name   string
		traces []chrometrace.Trace
	}{
		{
			name: "empty",
		},
		{
			name: "non-empty",
			traces: []chrometrace.Trace{
				{Name: "foo"},
				{Name: "bar"},
			},
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			runRPL2TraceCommand = func(ctx context.Context, rpl2TraceBin, rplPath, jsonPath string) error {
				f, err := os.Create(jsonPath)
				if err != nil {
					return err
				}
				return json.NewEncoder(f).Encode(tc.traces)
			}
			got, err := FromRPLFile(context.Background(), "", "", t.TempDir())
			if err != nil {
				t.Errorf("FromRPLFile got unexpected error: %v", err)
			}
			if diff := cmp.Diff(tc.traces, got); diff != "" {
				t.Errorf("FromRPLFile got diff (-want +got):\n%s", diff)
			}
		})
	}
}

func TestFromRPLFileError(t *testing.T) {
	origRunRPL2TraceCommand := runRPL2TraceCommand
	defer func() { runRPL2TraceCommand = origRunRPL2TraceCommand }()

	for _, tc := range []struct {
		name                string
		runRPL2TraceCommand func(context.Context, string, string, string) error
	}{
		{
			name: "command error",
			runRPL2TraceCommand: func(context.Context, string, string, string) error {
				return fmt.Errorf("test error")
			},
		},
		{
			name: "non-existant temporary JSON",
			runRPL2TraceCommand: func(context.Context, string, string, string) error {
				// Do nothing, don't error out, and don't write the JSON neither.
				return nil
			},
		},
		{
			name: "incorrect JSON format",
			runRPL2TraceCommand: func(_ context.Context, _, _, jsonPath string) error {
				return os.WriteFile(jsonPath, []byte("not valid JSON"), 0644)
			},
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if _, err := FromRPLFile(context.Background(), "", "", t.TempDir()); err == nil {
				t.Error("FromRPLFile got no error, want non-nil error")
			}
		})
	}
}
