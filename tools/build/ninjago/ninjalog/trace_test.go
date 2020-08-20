// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ninjalog

import (
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/compdb"
)

func TestTrace(t *testing.T) {
	flow := [][]Step{
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
	}

	traces := ToTraces(flow, 1)
	want := []Trace{
		{
			Name:      "resources/inspector/devtools_extension_api.js",
			Category:  "unknown",
			EventType: "X",
			Timestamp: 76 * 1000,
			Duration:  (187 - 76) * 1000,
			ProcessID: 1,
			ThreadID:  0,
			Args:      map[string]interface{}{},
		},
		{
			Name:      "gen/angle/commit_id.py",
			Category:  "unknown",
			EventType: "X",
			Timestamp: 78 * 1000,
			Duration:  (286 - 78) * 1000,
			ProcessID: 1,
			ThreadID:  1,
			Args:      map[string]interface{}{},
		},
		{
			Name:      "gen/angle/copy_compiler_dll.bat",
			Category:  "unknown",
			EventType: "X",
			Timestamp: 79 * 1000,
			Duration:  (287 - 79) * 1000,
			ProcessID: 1,
			ThreadID:  2,
			Args:      map[string]interface{}{},
		},
		{
			Name:      "gen/autofill_regex_constants.cc",
			Category:  "unknown",
			EventType: "X",
			Timestamp: 80 * 1000,
			Duration:  (284 - 80) * 1000,
			ProcessID: 1,
			ThreadID:  3,
			Args:      map[string]interface{}{},
		},
		{
			Name:      "PepperFlash/manifest.json",
			Category:  "unknown",
			EventType: "X",
			Timestamp: 141 * 1000,
			Duration:  (287 - 141) * 1000,
			ProcessID: 1,
			ThreadID:  4,
			Args:      map[string]interface{}{},
		},
		{
			Name:      "PepperFlash/libpepflashplayer.so",
			Category:  "unknown",
			EventType: "X",
			Timestamp: 142 * 1000,
			Duration:  (288 - 142) * 1000,
			ProcessID: 1,
			ThreadID:  5,
			Args:      map[string]interface{}{},
		},
		{
			Name:      "obj/third_party/pdfium/core/src/fpdfdoc/fpdfdoc.doc_formfield.o",
			Category:  "gomacc",
			EventType: "X",
			Timestamp: 187 * 1000,
			Duration:  (21304 - 187) * 1000,
			ProcessID: 1,
			ThreadID:  0,
			Args: map[string]interface{}{
				"command": "prebuilt/third_party/goma/linux-x64/gomacc some args and files",
			},
		},
		{
			Name:      "obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp",
			Category:  "touch",
			EventType: "X",
			Timestamp: 287 * 1000,
			Duration:  (290 - 287) * 1000,
			ProcessID: 1,
			ThreadID:  1,
			Args: map[string]interface{}{
				"command": "touch obj/third_party/angle/src/copy_scripts.actions_rules_copies.stamp",
			},
		},
	}

	if diff := cmp.Diff(want, traces); diff != "" {
		t.Errorf("ToTrace()=%#v\nwant=%#v\ndiff (-want +got):\n%s", traces, want, diff)
	}
}
