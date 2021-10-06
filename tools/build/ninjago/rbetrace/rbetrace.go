// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// package rbetrace contains utilities for working with RBE traces.
package rbetrace

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/build/ninjago/chrometrace"
)

// This function is stubbable in test.
var runRPL2TraceCommand = func(ctx context.Context, rpl2TraceBin, rplPath, jsonPath string) error {
	cmd := exec.CommandContext(
		ctx,
		rpl2TraceBin,
		"--log_path",
		"text://"+rplPath,
		"--output",
		jsonPath,
	)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("converting RPL %q to Chrome trace: %v", rplPath, err)
	}
	return nil
}

// FromRPLFile converts reproxy's RPL file containing reproxy's performance
// metrics to Chrome trace format.
//
// This function relies on reclient's rpl2trace binary for parsing RPL files,
// because currently there are no easy ways to depend on the proto schema
// reclient uses to write RPL files (RPL files are textprotos).
func FromRPLFile(ctx context.Context, rpl2TraceBin, rplPath, tmpDir string) ([]chrometrace.Trace, error) {
	tmpJSONPath := filepath.Join(tmpDir, "rbe_traces.json")
	if err := runRPL2TraceCommand(ctx, rpl2TraceBin, rplPath, tmpJSONPath); err != nil {
		return nil, err
	}

	var rbeTrace []chrometrace.Trace
	tmpJSON, err := os.Open(tmpJSONPath)
	if err != nil {
		return nil, fmt.Errorf("reading %q: %v", tmpJSONPath, err)
	}
	d := json.NewDecoder(tmpJSON)
	if err := d.Decode(&rbeTrace); err != nil {
		return nil, fmt.Errorf("parsing %q: %v", tmpJSONPath, err)
	}
	return rbeTrace, nil
}

type traceAndStartTime struct {
	// Records min start time of events for the same output to align them with the
	// corresponding event in the main trace.
	//
	// RBE uses epoch time as start time, while Ninja traces always starts from
	// time 0.
	minStartTime int
	traces       []chrometrace.Trace
}

// Interleave interleaves the input rbeTraces into mainTraces. Traces are joined
// on outputs, and timestamps are aligned.
func Interleave(mainTraces []chrometrace.Trace, rbeTraces []chrometrace.Trace) ([]chrometrace.Trace, error) {
	// Index all RBE traces by output. They will be joined with them main trace on
	// outputs.
	rbeTraceByOutputs := map[string]traceAndStartTime{}
	for _, rt := range rbeTraces {
		output := rt.Args["target"].(string)
		// Skip events with unknown outputs.
		if output == "" {
			continue
		}
		// Remove leading "/" if any, so they have a consistent format with outputs
		// from the main trace.
		if output[0] == '/' {
			output = output[1:]
		}
		// RBE can use depfiles as "target" for an action, while depfiles are not
		// considered as outputs by Ninja. By convention depfiles always match
		// output names without the `.d` suffix.
		//
		// TODO(https://fxbug.dev/85536): find a better way to handle this.
		output = strings.TrimSuffix(output, ".d")

		t, ok := rbeTraceByOutputs[output]
		if !ok {
			rbeTraceByOutputs[output] = traceAndStartTime{
				minStartTime: rt.TimestampMicros,
				traces:       []chrometrace.Trace{rt},
			}
		} else {
			if rt.TimestampMicros < t.minStartTime {
				t.minStartTime = rt.TimestampMicros
			}
			t.traces = append(t.traces, rt)
			rbeTraceByOutputs[output] = t
		}
	}

	// toInterleave is used to collect all RBE trace events to interleave into the
	// main trace.
	var toInterleave []chrometrace.Trace

	for _, mainTrace := range mainTraces {
		// We only care about complete events, see the chrometrace package for
		// details of different events.
		if mainTrace.EventType != chrometrace.CompleteEvent {
			continue
		}

		outputs, ok := mainTrace.Args["outputs"]
		if !ok {
			return nil, fmt.Errorf("no outputs found for action from main trace, this should not be possible if traces are generated through ninjatrace, full main trace event:\n%#v", mainTrace)
		}

		var rbeTrace traceAndStartTime
		var found bool
		for _, output := range outputs.([]string) {
			t, ok := rbeTraceByOutputs[output]
			if !ok {
				continue
			}
			// We've found a match before, this means there are multiple RBE traces
			// for the same action from the main trace. This should not be possible,
			// because RBE-enabled Ninja actions should have a 1:1 mapping with RBE
			// events.
			if found {
				return nil, fmt.Errorf("multiple RBE traces found for the same action from the main trace, full main trace event:\n%#v", mainTrace)
			}
			found = true
			rbeTrace = t
		}

		// No RBE traces found for this action, skip.
		if !found {
			continue
		}

		// Used to align RBE events with the corresponding event from the main trace.
		timeDelta := rbeTrace.minStartTime - mainTrace.TimestampMicros
		// Interleave RBE traces into the main trace by changing their process and
		// thread ID, and then align their start timestamps.
		for _, rt := range rbeTrace.traces {
			rt.ProcessID = mainTrace.ProcessID
			rt.ThreadID = mainTrace.ThreadID
			rt.TimestampMicros -= timeDelta
			toInterleave = append(toInterleave, rt)
		}
	}
	return append(mainTraces, toInterleave...), nil
}
