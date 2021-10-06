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

func TestInterleave(t *testing.T) {
	for _, tc := range []struct {
		name       string
		mainTraces []chrometrace.Trace
		rbeTraces  []chrometrace.Trace
		want       []chrometrace.Trace
	}{
		{
			name: "empty main traces",
			rbeTraces: []chrometrace.Trace{
				{Name: "foo", Args: map[string]interface{}{"target": "/path/to/foo"}},
			},
		},
		{
			name:       "empty RBE traces",
			mainTraces: []chrometrace.Trace{{Name: "foo"}},
			want:       []chrometrace.Trace{{Name: "foo"}},
		},
		{
			name: "aligns timestamps and process thread IDs",
			mainTraces: []chrometrace.Trace{
				{
					Name:            "foo",
					EventType:       chrometrace.CompleteEvent,
					ProcessID:       1,
					ThreadID:        1,
					TimestampMicros: 10,
					Args: map[string]interface{}{
						"outputs": []string{"foo"},
					},
				},
				{
					Name:            "bar",
					EventType:       chrometrace.CompleteEvent,
					ProcessID:       1,
					ThreadID:        2,
					TimestampMicros: 20,
					Args: map[string]interface{}{
						"outputs": []string{"bar"},
					},
				},
			},
			rbeTraces: []chrometrace.Trace{
				{
					Name:            "rbe_foo_1",
					EventType:       chrometrace.CompleteEvent,
					TimestampMicros: 100,
					Args:            map[string]interface{}{"target": "foo"},
				},
				{
					Name:            "rbe_foo_2",
					EventType:       chrometrace.CompleteEvent,
					TimestampMicros: 105,
					Args:            map[string]interface{}{"target": "foo"},
				},
				{
					Name:            "rbe_bar_1",
					EventType:       chrometrace.CompleteEvent,
					TimestampMicros: 200,
					Args:            map[string]interface{}{"target": "bar"},
				},
			},
			want: []chrometrace.Trace{
				{
					Name:            "foo",
					EventType:       chrometrace.CompleteEvent,
					ProcessID:       1,
					ThreadID:        1,
					TimestampMicros: 10,
					Args: map[string]interface{}{
						"outputs": []string{"foo"},
					},
				},
				{
					Name:            "bar",
					EventType:       chrometrace.CompleteEvent,
					ProcessID:       1,
					ThreadID:        2,
					TimestampMicros: 20,
					Args: map[string]interface{}{
						"outputs": []string{"bar"},
					},
				},
				{
					Name:            "rbe_foo_1",
					EventType:       chrometrace.CompleteEvent,
					ProcessID:       1,
					ThreadID:        1,
					TimestampMicros: 10,
					Args:            map[string]interface{}{"target": "foo"},
				},
				{
					Name:            "rbe_foo_2",
					EventType:       chrometrace.CompleteEvent,
					ProcessID:       1,
					ThreadID:        1,
					TimestampMicros: 15,
					Args:            map[string]interface{}{"target": "foo"},
				},
				{
					Name:            "rbe_bar_1",
					EventType:       chrometrace.CompleteEvent,
					ProcessID:       1,
					ThreadID:        2,
					TimestampMicros: 20,
					Args:            map[string]interface{}{"target": "bar"},
				},
			},
		},
		{
			name: "properly handle depfiles",
			mainTraces: []chrometrace.Trace{
				{
					Name:      "foo",
					EventType: chrometrace.CompleteEvent,
					Args: map[string]interface{}{
						"outputs": []string{"foo"},
					},
				},
			},
			rbeTraces: []chrometrace.Trace{
				{
					Name:      "rbe_foo_depfile_1",
					EventType: chrometrace.CompleteEvent,
					Args:      map[string]interface{}{"target": "foo.d"},
				},
				{
					Name:      "rbe_foo_2_depfile_2",
					EventType: chrometrace.CompleteEvent,
					Args:      map[string]interface{}{"target": "foo.d"},
				},
			},
			want: []chrometrace.Trace{
				{
					Name:      "foo",
					EventType: chrometrace.CompleteEvent,
					Args: map[string]interface{}{
						"outputs": []string{"foo"},
					},
				},
				{
					Name:      "rbe_foo_depfile_1",
					EventType: chrometrace.CompleteEvent,
					Args:      map[string]interface{}{"target": "foo.d"},
				},
				{
					Name:      "rbe_foo_2_depfile_2",
					EventType: chrometrace.CompleteEvent,
					Args:      map[string]interface{}{"target": "foo.d"},
				},
			},
		},
		{
			name: "multiple outputs",
			mainTraces: []chrometrace.Trace{
				{
					Name:      "foobar",
					EventType: chrometrace.CompleteEvent,
					Args: map[string]interface{}{
						"outputs": []string{"foo", "bar"},
					},
				},
			},
			rbeTraces: []chrometrace.Trace{
				{
					Name:      "rbe_bar_1",
					EventType: chrometrace.CompleteEvent,
					Args:      map[string]interface{}{"target": "bar"},
				},
				{
					Name:      "rbe_bar_2",
					EventType: chrometrace.CompleteEvent,
					Args:      map[string]interface{}{"target": "bar"},
				},
			},
			want: []chrometrace.Trace{
				{
					Name:      "foobar",
					EventType: chrometrace.CompleteEvent,
					Args: map[string]interface{}{
						"outputs": []string{"foo", "bar"},
					},
				},
				{
					Name:      "rbe_bar_1",
					EventType: chrometrace.CompleteEvent,
					Args:      map[string]interface{}{"target": "bar"},
				},
				{
					Name:      "rbe_bar_2",
					EventType: chrometrace.CompleteEvent,
					Args:      map[string]interface{}{"target": "bar"},
				},
			},
		},
		{
			name: "ignore non-complete events",
			mainTraces: []chrometrace.Trace{
				{
					Name:      "no_interleave",
					EventType: chrometrace.FlowEventStart,
					Args: map[string]interface{}{
						"outputs": []string{"foo", "bar"},
					},
				},
			},
			rbeTraces: []chrometrace.Trace{
				{
					Name:      "rbe_foo",
					EventType: chrometrace.CompleteEvent,
					Args:      map[string]interface{}{"target": "foo"},
				},
			},
			want: []chrometrace.Trace{
				{
					Name:      "no_interleave",
					EventType: chrometrace.FlowEventStart,
					Args: map[string]interface{}{
						"outputs": []string{"foo", "bar"},
					},
				},
			},
		},
		{
			name: "removes leading slash from rbe trace paths",
			mainTraces: []chrometrace.Trace{
				{
					Name:      "foo",
					EventType: chrometrace.CompleteEvent,
					Args: map[string]interface{}{
						"outputs": []string{"path/to/foo"},
					},
				},
			},
			rbeTraces: []chrometrace.Trace{
				{
					Name:      "rbe_foo",
					EventType: chrometrace.CompleteEvent,
					Args:      map[string]interface{}{"target": "/path/to/foo"},
				},
			},
			want: []chrometrace.Trace{
				{
					Name:      "foo",
					EventType: chrometrace.CompleteEvent,
					Args: map[string]interface{}{
						"outputs": []string{"path/to/foo"},
					},
				},
				{
					Name:      "rbe_foo",
					EventType: chrometrace.CompleteEvent,
					Args:      map[string]interface{}{"target": "/path/to/foo"},
				},
			},
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			got, err := Interleave(tc.mainTraces, tc.rbeTraces)
			if err != nil {
				t.Errorf("Interleave(\n%#v,\n%#v\n) error: %v", tc.mainTraces, tc.rbeTraces, err)
			}
			if diff := cmp.Diff(tc.want, got); diff != "" {
				t.Errorf("Interleave(\n%#v,\n%#v\n) got diff (-want +got):\n%s", tc.mainTraces, tc.rbeTraces, diff)
			}
		})
	}
}

func TestInterleaveError(t *testing.T) {
	for _, tc := range []struct {
		name       string
		mainTraces []chrometrace.Trace
		rbeTraces  []chrometrace.Trace
	}{
		{
			name: "missing outputs from main traces",
			mainTraces: []chrometrace.Trace{
				{
					Name:      "missing_outputs",
					EventType: chrometrace.CompleteEvent,
				},
			},
		},
		{
			// When several RBE trace events match different outputs from the same
			// action.
			name: "conflicting RBE trace events",
			mainTraces: []chrometrace.Trace{
				{
					Name:      "foobar",
					EventType: chrometrace.CompleteEvent,
					Args: map[string]interface{}{
						"outputs": []string{"foo", "bar"},
					},
				},
			},
			rbeTraces: []chrometrace.Trace{
				{
					Name:      "rbe_foo",
					EventType: chrometrace.CompleteEvent,
					Args:      map[string]interface{}{"target": "foo"},
				},
				{
					Name:      "rbe_bar",
					EventType: chrometrace.CompleteEvent,
					Args:      map[string]interface{}{"target": "bar"},
				},
			},
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if _, err := Interleave(tc.mainTraces, tc.rbeTraces); err == nil {
				t.Error("Interleave got no error, want non-nil error")
			}
		})
	}
}
