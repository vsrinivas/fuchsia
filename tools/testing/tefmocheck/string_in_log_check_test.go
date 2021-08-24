// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"fmt"
	"path"
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
		String:         killerString,
		OnlyOnStates:   []string{},
		ExceptString:   exceptString,
		ExceptBlocks:   []*logBlock{exceptBlock, exceptBlock2},
		SkipPassedTask: true,
		Type:           swarmingOutputType,
	}
	gotName := c.Name()
	wantName := "string_in_log/infra_and_test_std_and_klog.txt/KILLER_STRING"
	if gotName != wantName {
		t.Errorf("c.Name() returned %q, want %q", gotName, wantName)
	}

	for _, tc := range []struct {
		name                string
		attributeToTest     bool
		testingOutputs      TestingOutputs
		states              []string
		swarmingResultState string
		shouldMatch         bool
		wantName            string
	}{
		{
			name: "should match simple",
			testingOutputs: TestingOutputs{
				SwarmingOutput: []byte(fmt.Sprintf("PREFIX %s SUFFIX", killerString)),
			},
			shouldMatch: true,
		},
		{
			name: "exceptSuccessfulSwarmingResult",
			testingOutputs: TestingOutputs{
				SwarmingOutput: []byte(fmt.Sprintf("PREFIX %s SUFFIX", killerString)),
			},
			swarmingResultState: "COMPLETED",
		},
		{
			name: "should not match if string in other log",
			testingOutputs: TestingOutputs{
				SerialLog:      []byte(killerString),
				SwarmingOutput: []byte("gentle string"),
			},
		},
		{
			name: "should not match if except_string in log",
			testingOutputs: TestingOutputs{
				SwarmingOutput: []byte(killerString + exceptString),
			},
		},
		{
			name: "should match if string before except_block",
			testingOutputs: TestingOutputs{
				SwarmingOutput: []byte(fmt.Sprintf("PREFIX %s ... %s output %s SUFFIX", killerString, exceptBlock.startString, exceptBlock.endString)),
			},
			shouldMatch: true,
		},
		{
			name: "should match if string after except_block",
			testingOutputs: TestingOutputs{
				SwarmingOutput: []byte(fmt.Sprintf("PREFIX %s output %s ... %s SUFFIX", exceptBlock.startString, exceptBlock.endString, killerString)),
			},
			shouldMatch: true,
		},
		{
			name: "should not match if string in except_block",
			testingOutputs: TestingOutputs{
				SwarmingOutput: []byte(
					fmt.Sprintf(
						"PREFIX %s %s output %s SUFFIX %s %s %s",
						exceptBlock.startString, killerString, exceptBlock.endString,
						exceptBlock2.startString, killerString, exceptBlock2.endString)),
			},
		},
		{
			name: "should match if string in both except_block and outside except_block",
			testingOutputs: TestingOutputs{
				SwarmingOutput: []byte(fmt.Sprintf(
					"PREFIX %s ... %s %s %s %s %s %s SUFFIX",
					killerString, exceptBlock.startString, killerString, exceptBlock.endString,
					exceptBlock2.startString, killerString, exceptBlock2.endString,
				)),
			},
			shouldMatch: true,
		},
		{
			name: "should match if swarming task state is in expected states",
			testingOutputs: TestingOutputs{
				SwarmingOutput: []byte(killerString),
			},
			states:              []string{"STATE_1", "STATE_2"},
			swarmingResultState: "STATE_1",
			shouldMatch:         true,
		},
		{
			name: "should not match if swarming task state is not in expected states",
			testingOutputs: TestingOutputs{
				SwarmingOutput: []byte(killerString),
			},
			states:              []string{"STATE_1", "STATE_2"},
			swarmingResultState: "NO_STATE",
		},
		{
			name:            "should match per test swarming output",
			attributeToTest: true,
			testingOutputs: TestingOutputs{
				SwarmingOutputPerTest: []TestLog{
					{
						TestName: "foo-test",
						Bytes:    []byte(killerString),
						FilePath: "foo/log.txt",
					},
				},
			},
			shouldMatch: true,
			wantName:    path.Join(wantName, "foo-test"),
		},
	} {
		t.Run(tc.name, func(t *testing.T) {
			c := c // Make a copy to avoid modifying shared state.
			c.AttributeToTest = tc.attributeToTest
			// It accesses this field for DebugText().
			tc.testingOutputs.SwarmingSummary = &SwarmingTaskSummary{
				Results: &SwarmingRpcsTaskResult{
					TaskId: "abc", State: tc.swarmingResultState,
				},
			}
			c.OnlyOnStates = tc.states
			if c.Check(&tc.testingOutputs) != tc.shouldMatch {
				t.Errorf("c.Check(%q) returned %t, expected %t",
					string(tc.testingOutputs.SerialLog), !tc.shouldMatch, tc.shouldMatch)
			}
			gotName := c.Name()
			if tc.wantName != "" && gotName != tc.wantName {
				t.Errorf("c.Name() returned %q, want %q", gotName, tc.wantName)
			}
			c.DebugText() // minimal coverage, check it doesn't crash.
			swarmingOutputPerTest := tc.testingOutputs.SwarmingOutputPerTest
			gotOutputFiles := c.OutputFiles()
			if len(swarmingOutputPerTest) == 0 && len(gotOutputFiles) != 0 {
				t.Errorf("c.OutputFiles() returned %s, want []", gotOutputFiles)
			}
			if len(swarmingOutputPerTest) == 1 && (len(gotOutputFiles) != 1 || swarmingOutputPerTest[0].FilePath != gotOutputFiles[0]) {
				t.Errorf("c.OutputFiles() returned %s, want %q", gotOutputFiles, swarmingOutputPerTest[0].FilePath)
			}
		})
	}
}
