// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ninjalog

import (
	"encoding/json"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/compdb"
)

func TestTrace(t *testing.T) {
	for _, tc := range []struct {
		desc         string
		flow         [][]Step
		criticalPath []Step
		want         []Trace
	}{
		{
			desc: "empty",
		},
		{
			desc: "without critical path",
			flow: [][]Step{
				{
					{
						Start:   76 * time.Millisecond,
						End:     187 * time.Millisecond,
						Out:     "resources/inspector/devtools_extension_api.js",
						CmdHash: 0x75430546595be7c2,
					},
					{
						Start:   187 * time.Millisecond,
						End:     21304 * time.Millisecond,
						Out:     "obj/third_party/pdfium/core/src/fpdfdoc/fpdfdoc.doc_formfield.o",
						CmdHash: 0x2ac7111aa1ae86af,
						Command: &compdb.Command{
							Command: "prebuilt/third_party/goma/linux-x64/gomacc some args and files",
						},
					},
				},
				{
					{
						Start:   78 * time.Millisecond,
						End:     286 * time.Millisecond,
						Out:     "gen/angle/commit_id.py",
						CmdHash: 0x4ede38e2c1617d8c,
					},
					{
						Start:   287 * time.Millisecond,
						End:     290 * time.Millisecond,
						Out:     "obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp",
						CmdHash: 0xb211d373de72f455,
						Command: &compdb.Command{
							Command: "touch obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp",
						},
					},
				},
				{
					{
						Start:   79 * time.Millisecond,
						End:     287 * time.Millisecond,
						Out:     "gen/angle/copy_compiler_dll.bat",
						CmdHash: 0x9fb635ad5d2c1109,
					},
				},
				{
					{
						Start:   80 * time.Millisecond,
						End:     284 * time.Millisecond,
						Out:     "gen/autofill_regex_constants.cc",
						CmdHash: 0xfa33c8d7ce1d8791,
					},
				},
				{
					{
						Start:   141 * time.Millisecond,
						End:     287 * time.Millisecond,
						Out:     "PepperFlash/manifest.json",
						CmdHash: 0x324f0a0b77c37ef,
					},
				},
				{
					{
						Start:   142 * time.Millisecond,
						End:     288 * time.Millisecond,
						Out:     "PepperFlash/libpepflashplayer.so",
						CmdHash: 0x1e2c2b7845a4d4fe,
					},
				},
			},
			want: []Trace{
				{
					Name:            "resources/inspector/devtools_extension_api.js",
					Category:        "unknown",
					EventType:       completeEvent,
					TimestampMicros: 76 * 1000,
					DurationMicros:  (187 - 76) * 1000,
					ProcessID:       1,
					ThreadID:        0,
				},
				{
					Name:            "gen/angle/commit_id.py",
					Category:        "unknown",
					EventType:       completeEvent,
					TimestampMicros: 78 * 1000,
					DurationMicros:  (286 - 78) * 1000,
					ProcessID:       1,
					ThreadID:        1,
				},
				{
					Name:            "gen/angle/copy_compiler_dll.bat",
					Category:        "unknown",
					EventType:       completeEvent,
					TimestampMicros: 79 * 1000,
					DurationMicros:  (287 - 79) * 1000,
					ProcessID:       1,
					ThreadID:        2,
				},
				{
					Name:            "gen/autofill_regex_constants.cc",
					Category:        "unknown",
					EventType:       completeEvent,
					TimestampMicros: 80 * 1000,
					DurationMicros:  (284 - 80) * 1000,
					ProcessID:       1,
					ThreadID:        3,
				},
				{
					Name:            "PepperFlash/manifest.json",
					Category:        "unknown",
					EventType:       completeEvent,
					TimestampMicros: 141 * 1000,
					DurationMicros:  (287 - 141) * 1000,
					ProcessID:       1,
					ThreadID:        4,
				},
				{
					Name:            "PepperFlash/libpepflashplayer.so",
					Category:        "unknown",
					EventType:       completeEvent,
					TimestampMicros: 142 * 1000,
					DurationMicros:  (288 - 142) * 1000,
					ProcessID:       1,
					ThreadID:        5,
				},
				{
					Name:            "obj/third_party/pdfium/core/src/fpdfdoc/fpdfdoc.doc_formfield.o",
					Category:        "gomacc",
					EventType:       completeEvent,
					TimestampMicros: 187 * 1000,
					DurationMicros:  (21304 - 187) * 1000,
					ProcessID:       1,
					ThreadID:        0,
					Args: map[string]interface{}{
						"command": "prebuilt/third_party/goma/linux-x64/gomacc some args and files",
					},
				},
				{
					Name:            "obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp",
					Category:        "touch",
					EventType:       completeEvent,
					TimestampMicros: 287 * 1000,
					DurationMicros:  (290 - 287) * 1000,
					ProcessID:       1,
					ThreadID:        1,
					Args: map[string]interface{}{
						"command": "touch obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp",
					},
				},
			},
		},
		{
			desc: "with critical path",
			flow: [][]Step{
				{
					{
						End: 5 * time.Microsecond,
						Out: "a",
					},
				},
				{
					{
						Start: 1 * time.Microsecond,
						End:   2 * time.Microsecond,
						Out:   "b",
					},
					{
						Start: 10 * time.Microsecond,
						End:   20 * time.Microsecond,
						Out:   "critical_c",
					},
				},
				{
					{
						Start: 3 * time.Microsecond,
						End:   5 * time.Microsecond,
						Out:   "critical_a",
					},
					{
						Start: 5 * time.Microsecond,
						End:   10 * time.Microsecond,
						Out:   "critical_b",
					},
				},
			},
			criticalPath: []Step{
				{
					Start: 3 * time.Microsecond,
					End:   5 * time.Microsecond,
					Out:   "critical_a",
				},
				{
					Start: 5 * time.Microsecond,
					End:   10 * time.Microsecond,
					Out:   "critical_b",
				},
				{
					Start: 10 * time.Microsecond,
					End:   20 * time.Microsecond,
					Out:   "critical_c",
				},
			},
			want: []Trace{
				{
					Name:            "a",
					Category:        "unknown",
					EventType:       completeEvent,
					TimestampMicros: 0,
					DurationMicros:  5,
					ProcessID:       1,
					ThreadID:        2,
				},
				{
					Name:            "b",
					Category:        "unknown",
					EventType:       completeEvent,
					TimestampMicros: 1,
					DurationMicros:  1,
					ProcessID:       1,
					ThreadID:        0,
				},

				{
					Name:            "critical_a",
					Category:        "unknown,critical_path",
					EventType:       completeEvent,
					TimestampMicros: 3,
					DurationMicros:  2,
					ProcessID:       1,
					ThreadID:        1,
				},
				{
					// Flow event going out of critical_a.
					Name:            "critical_path",
					Category:        "critical_path",
					EventType:       flowEventStart,
					TimestampMicros: 4,
					ProcessID:       1,
					ThreadID:        1,
					ID:              1,
				},

				{
					Name:            "critical_b",
					Category:        "unknown,critical_path",
					EventType:       completeEvent,
					TimestampMicros: 5,
					DurationMicros:  5,
					ProcessID:       1,
					ThreadID:        1,
				},
				{
					// Flow event going out of critical_b.
					Name:            "critical_path",
					Category:        "critical_path",
					EventType:       flowEventStart,
					TimestampMicros: 7,
					ProcessID:       1,
					ThreadID:        1,
					ID:              2,
				},
				{
					// Flow event pointing to critical_b.
					Name:            "critical_path",
					Category:        "critical_path",
					EventType:       flowEventEnd,
					TimestampMicros: 7,
					ProcessID:       1,
					ThreadID:        1,
					ID:              1,
					BindingPoint:    "e",
				},

				{
					Name:            "critical_c",
					Category:        "unknown,critical_path",
					EventType:       completeEvent,
					TimestampMicros: 10,
					DurationMicros:  10,
					ProcessID:       1,
					ThreadID:        0,
				},
				{
					// Flow event pointing to critical_c.
					Name:            "critical_path",
					Category:        "critical_path",
					EventType:       flowEventEnd,
					TimestampMicros: 15,
					ProcessID:       1,
					ThreadID:        0,
					ID:              2,
					BindingPoint:    "e",
				},
			},
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			got := ToTraces(tc.flow, 1, tc.criticalPath)
			if diff := cmp.Diff(tc.want, got); diff != "" {
				t.Errorf("ToTrace()=%#v\nwant=%#v\ndiff (-want +got):\n%s", got, tc.want, diff)
			}
		})
	}
}

func TestClangTracesToInterleave(t *testing.T) {
	for _, tc := range []struct {
		desc        string
		traces      []Trace
		granularity time.Duration
		clangTraces map[string]clangTrace
		want        []Trace
		wantErr     bool
	}{
		{
			desc: "empty",
		},
		{
			desc: "successfully interleave",
			traces: []Trace{
				{TimestampMicros: 42, DurationMicros: 1000, Name: "output.cc.o", ProcessID: 123, ThreadID: 321},
			},
			granularity: 100 * time.Microsecond,
			clangTraces: map[string]clangTrace{
				"output.cc.json": {
					TraceEvents: []Trace{
						{TimestampMicros: 0, DurationMicros: 1000, Name: "ExecuteCompiler", EventType: completeEvent, ProcessID: 789, ThreadID: 1},
						{TimestampMicros: 0, DurationMicros: 100, Name: "Source", EventType: completeEvent, ProcessID: 789, ThreadID: 2},
						{TimestampMicros: 100, DurationMicros: 120, Name: "Source", EventType: completeEvent, ProcessID: 789, ThreadID: 2},
						{TimestampMicros: 300, DurationMicros: 420, Name: "Frontend", EventType: completeEvent, ProcessID: 789, ThreadID: 3},
						// Events below should be filtered.
						{TimestampMicros: 0, DurationMicros: 800, Name: "NotComplete", ProcessID: 789, ThreadID: 4},
						{TimestampMicros: 0, DurationMicros: 10, Name: "TooShort", EventType: completeEvent, ProcessID: 789, ThreadID: 5},
					},
				},
			},
			want: []Trace{
				{TimestampMicros: 42, DurationMicros: 1000, Name: "ExecuteCompiler", EventType: completeEvent, ProcessID: 123, ThreadID: 321},
				{TimestampMicros: 42, DurationMicros: 100, Name: "Source", EventType: completeEvent, ProcessID: 123, ThreadID: 321},
				{TimestampMicros: 142, DurationMicros: 120, Name: "Source", EventType: completeEvent, ProcessID: 123, ThreadID: 321},
				{TimestampMicros: 342, DurationMicros: 420, Name: "Frontend", EventType: completeEvent, ProcessID: 123, ThreadID: 321},
			},
		},
		{
			desc: "outputs missing clang traces are skipped",
			traces: []Trace{
				{TimestampMicros: 42, DurationMicros: 1000, Name: "output.cc.o", ProcessID: 123, ThreadID: 321},
				{TimestampMicros: 100, DurationMicros: 2000, Name: "missing_trace.cc.o", ProcessID: 234, ThreadID: 432},
				{TimestampMicros: 200, DurationMicros: 900, Name: "another_output.cc.o", ProcessID: 345, ThreadID: 543},
			},
			granularity: 100 * time.Microsecond,
			clangTraces: map[string]clangTrace{
				"output.cc.json": {
					TraceEvents: []Trace{
						{TimestampMicros: 0, DurationMicros: 1000, Name: "ExecuteCompiler", EventType: completeEvent, ProcessID: 789, ThreadID: 1},
					},
				},
				"another_output.cc.json": {
					TraceEvents: []Trace{
						{TimestampMicros: 100, DurationMicros: 200, Name: "ExecuteCompiler", EventType: completeEvent, ProcessID: 789, ThreadID: 1},
					},
				},
			},
			want: []Trace{
				{TimestampMicros: 42, DurationMicros: 1000, Name: "ExecuteCompiler", EventType: completeEvent, ProcessID: 123, ThreadID: 321},
				{TimestampMicros: 300, DurationMicros: 200, Name: "ExecuteCompiler", EventType: completeEvent, ProcessID: 345, ThreadID: 543},
			},
		},
		{
			desc: "sub event longer than main step",
			traces: []Trace{
				{TimestampMicros: 42, DurationMicros: 1000, Name: "output.cc.o", ProcessID: 123, ThreadID: 321},
			},
			granularity: 100 * time.Microsecond,
			clangTraces: map[string]clangTrace{
				"output.cc.json": {
					TraceEvents: []Trace{
						{TimestampMicros: 0, DurationMicros: 999999, Name: "ExecuteCompiler", EventType: completeEvent, ProcessID: 789, ThreadID: 1},
					},
				},
			},
			wantErr: true,
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			tmpDir, err := ioutil.TempDir("", "TestClangTracesToInterleave")
			if err != nil {
				t.Fatalf("Failed to create temp dir for test clang traces: %v", err)
			}
			defer func() {
				if err := os.RemoveAll(tmpDir); err != nil {
					t.Fatalf("Failed to remove temp dir %s after test: %v", tmpDir, err)
				}
			}()

			for filename, trace := range tc.clangTraces {
				p := filepath.Join(tmpDir, filename)
				traceFile, err := os.Create(p)
				if err != nil {
					t.Fatalf("Failed to create test clang trace at path %s: %v", p, err)
				}
				if err := json.NewEncoder(traceFile).Encode(trace); err != nil {
					t.Fatalf("Failed to write to test clang trace to file %s: %v", p, err)
				}
			}

			got, err := ClangTracesToInterleave(tc.traces, tmpDir, tc.granularity)
			if (err != nil) != tc.wantErr {
				t.Errorf("ClangTracesToInterleave got error: %v, want error: %t", err, tc.wantErr)
			}
			if diff := cmp.Diff(tc.want, got); diff != "" {
				t.Errorf("ClangTracesToInterleave got: %#v, want: %#v, diff (-want, +got):\n%s", got, tc.want, diff)
			}
		})
	}
}
