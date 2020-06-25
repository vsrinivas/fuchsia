// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"testing"
)

func TestTaskStateCheck(t *testing.T) {
	const stateName = "STATE_NAME"
	c := taskStateCheck{State: stateName}
	gotName := c.Name()
	wantName := "task_state/" + stateName
	if gotName != wantName {
		t.Errorf("c.Name() returned %q, want %q", gotName, wantName)
	}
	shouldMatch := TestingOutputs{
		SwarmingSummary: &SwarmingTaskSummary{
			Results: &SwarmingRpcsTaskResult{
				State: stateName,
			},
		},
	}
	if !c.Check(&shouldMatch) {
		t.Errorf("c.Check(%q) returned false, expected true", stateName)
	}
	shouldNotMatch := TestingOutputs{
		SwarmingSummary: &SwarmingTaskSummary{
			Results: &SwarmingRpcsTaskResult{
				State: "NOT_" + stateName,
			},
		},
	}
	if c.Check(&shouldNotMatch) {
		t.Errorf("c.Check(%q) returned true, expected false", "NOT_"+stateName)
	}
	if len(c.DebugText()) == 0 {
		t.Error("c.DebugText() returned empty string")
	}
}
