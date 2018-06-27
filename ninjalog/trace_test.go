// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ninjalog

import (
	"reflect"
	"testing"
	"time"
)

func TestTrace(t *testing.T) {
	flow := [][]Step{
		[]Step{
			Step{
				Start:   76 * time.Millisecond,
				End:     187 * time.Millisecond,
				Out:     "resources/inspector/devtools_extension_api.js",
				CmdHash: "75430546595be7c2",
			},
			Step{
				Start:   187 * time.Millisecond,
				End:     21304 * time.Millisecond,
				Out:     "obj/third_party/pdfium/core/src/fpdfdoc/fpdfdoc.doc_formfield.o",
				CmdHash: "2ac7111aa1ae86af",
			},
		},
		[]Step{
			Step{
				Start:   78 * time.Millisecond,
				End:     286 * time.Millisecond,
				Out:     "gen/angle/commit_id.py",
				CmdHash: "4ede38e2c1617d8c",
			},
			Step{
				Start:   287 * time.Millisecond,
				End:     290 * time.Millisecond,
				Out:     "obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp",
				CmdHash: "b211d373de72f455",
			},
		},
		[]Step{
			Step{
				Start:   79 * time.Millisecond,
				End:     287 * time.Millisecond,
				Out:     "gen/angle/copy_compiler_dll.bat",
				CmdHash: "9fb635ad5d2c1109",
			},
		},
		[]Step{
			Step{
				Start:   80 * time.Millisecond,
				End:     284 * time.Millisecond,
				Out:     "gen/autofill_regex_constants.cc",
				CmdHash: "fa33c8d7ce1d8791",
			},
		},
		[]Step{
			Step{
				Start:   141 * time.Millisecond,
				End:     287 * time.Millisecond,
				Out:     "PepperFlash/manifest.json",
				CmdHash: "324f0a0b77c37ef",
			},
		},
		[]Step{
			Step{
				Start:   142 * time.Millisecond,
				End:     288 * time.Millisecond,
				Out:     "PepperFlash/libpepflashplayer.so",
				CmdHash: "1e2c2b7845a4d4fe",
			},
		},
	}

	traces := ToTraces(flow, 1)
	want := []Trace{
		Trace{
			Name:      "resources/inspector/devtools_extension_api.js",
			Category:  "target",
			EventType: "X",
			Timestamp: 76 * 1000,
			Duration:  (187 - 76) * 1000,
			ProcessID: 1,
			ThreadID:  0,
			Args:      map[string]interface{}{},
		},
		Trace{
			Name:      "gen/angle/commit_id.py",
			Category:  "target",
			EventType: "X",
			Timestamp: 78 * 1000,
			Duration:  (286 - 78) * 1000,
			ProcessID: 1,
			ThreadID:  1,
			Args:      map[string]interface{}{},
		},
		Trace{
			Name:      "gen/angle/copy_compiler_dll.bat",
			Category:  "target",
			EventType: "X",
			Timestamp: 79 * 1000,
			Duration:  (287 - 79) * 1000,
			ProcessID: 1,
			ThreadID:  2,
			Args:      map[string]interface{}{},
		},
		Trace{
			Name:      "gen/autofill_regex_constants.cc",
			Category:  "target",
			EventType: "X",
			Timestamp: 80 * 1000,
			Duration:  (284 - 80) * 1000,
			ProcessID: 1,
			ThreadID:  3,
			Args:      map[string]interface{}{},
		},
		Trace{
			Name:      "PepperFlash/manifest.json",
			Category:  "target",
			EventType: "X",
			Timestamp: 141 * 1000,
			Duration:  (287 - 141) * 1000,
			ProcessID: 1,
			ThreadID:  4,
			Args:      map[string]interface{}{},
		},
		Trace{
			Name:      "PepperFlash/libpepflashplayer.so",
			Category:  "target",
			EventType: "X",
			Timestamp: 142 * 1000,
			Duration:  (288 - 142) * 1000,
			ProcessID: 1,
			ThreadID:  5,
			Args:      map[string]interface{}{},
		},
		Trace{
			Name:      "obj/third_party/pdfium/core/src/fpdfdoc/fpdfdoc.doc_formfield.o",
			Category:  "target",
			EventType: "X",
			Timestamp: 187 * 1000,
			Duration:  (21304 - 187) * 1000,
			ProcessID: 1,
			ThreadID:  0,
			Args:      map[string]interface{}{},
		},
		Trace{
			Name:      "obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp",
			Category:  "target",
			EventType: "X",
			Timestamp: 287 * 1000,
			Duration:  (290 - 287) * 1000,
			ProcessID: 1,
			ThreadID:  1,
			Args:      map[string]interface{}{},
		},
	}

	if !reflect.DeepEqual(traces, want) {
		t.Errorf("ToTrace()=%v; want=%v", traces, want)
	}
}
