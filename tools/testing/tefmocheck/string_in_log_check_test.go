// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"fmt"
	"testing"
)

func TestStringInLogCheck(t *testing.T) {
	const killerString = "KILLER STRING"
	const exceptString = "Don't die!"
	exceptBlock := &logBlock{
		startString: "skip_test",
		endString:   "end_test",
	}
	exceptBlock2 := &logBlock{
		startString: "block_start",
		endString:   "block_end",
	}
	c := stringInLogCheck{
		String:                         killerString,
		ExceptString:                   exceptString,
		ExceptBlocks:                   []*logBlock{exceptBlock, exceptBlock2},
		ExceptSuccessfulSwarmingResult: true,
		Type:                           serialLogType,
	}
	gotName := c.Name()
	wantName := "string_in_log/serial_log.txt/KILLER_STRING"
	if gotName != wantName {
		t.Errorf("c.Name() returned %q, want %q", gotName, wantName)
	}

	for _, tc := range []struct {
		name                string
		testingOutputs      TestingOutputs
		swarmingResultState string
		shouldMatch         bool
	}{
		{
			name: "should match if string in serial",
			testingOutputs: TestingOutputs{
				SerialLog: []byte(fmt.Sprintf("PREFIX %s SUFFIX", killerString)),
			},
			shouldMatch: true,
		},
		{
			name: "exceptSuccessfulSwarmingResult",
			testingOutputs: TestingOutputs{
				SerialLog: []byte(fmt.Sprintf("PREFIX %s SUFFIX", killerString)),
			},
			swarmingResultState: "COMPLETED",
			shouldMatch:         false,
		}, {
			name: "should not match if string in other log",
			testingOutputs: TestingOutputs{
				SerialLog:      []byte("gentle string"),
				SwarmingOutput: []byte(killerString),
			},
			shouldMatch: false,
		}, {
			name: "should not match if except_string in log",
			testingOutputs: TestingOutputs{
				SerialLog: []byte(killerString + exceptString),
			},
			shouldMatch: false,
		}, {
			name: "should match if string before except_block",
			testingOutputs: TestingOutputs{
				SerialLog: []byte(fmt.Sprintf("PREFIX %s ... %s output %s SUFFIX", killerString, exceptBlock.startString, exceptBlock.endString)),
			},
			shouldMatch: true,
		}, {
			name: "should match if string after except_block",
			testingOutputs: TestingOutputs{
				SerialLog: []byte(fmt.Sprintf("PREFIX %s output %s ... %s SUFFIX", exceptBlock.startString, exceptBlock.endString, killerString)),
			},
			shouldMatch: true,
		}, {
			name: "should not match if string in except_block",
			testingOutputs: TestingOutputs{
				SerialLog: []byte(
					fmt.Sprintf(
						"PREFIX %s %s output %s SUFFIX %s %s %s",
						exceptBlock.startString, killerString, exceptBlock.endString,
						exceptBlock2.startString, killerString, exceptBlock2.endString)),
			},
			shouldMatch: false,
		}, {
			name: "should match if string in both except_block and outside except_block",
			testingOutputs: TestingOutputs{
				SerialLog: []byte(fmt.Sprintf(
					"PREFIX %s ... %s %s %s %s %s %s SUFFIX",
					killerString, exceptBlock.startString, killerString, exceptBlock.endString,
					exceptBlock2.startString, killerString, exceptBlock2.endString,
				)),
			},
			shouldMatch: true,
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			// It accesses this field for DebugText().
			tc.testingOutputs.SwarmingSummary = &SwarmingTaskSummary{Results: &SwarmingRpcsTaskResult{TaskId: "abc", State: tc.swarmingResultState}}
			if c.Check(&tc.testingOutputs) != tc.shouldMatch {
				t.Errorf("c.Check(%q) returned %v, expected %v", string(tc.testingOutputs.SerialLog), !tc.shouldMatch, tc.shouldMatch)
			}
			c.DebugText() // minimal coverage, check it doesn't crash.
		})
	}
}
