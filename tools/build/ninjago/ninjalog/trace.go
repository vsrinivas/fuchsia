// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ninjalog

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// Trace is an entry of trace format.
// https://code.google.com/p/trace-viewer/
type Trace struct {
	Name            string                 `json:"name"`
	Category        string                 `json:"cat"`
	EventType       string                 `json:"ph"`
	TimestampMicros int                    `json:"ts"`
	DurationMicros  int                    `json:"dur"`
	ProcessID       int                    `json:"pid"`
	ThreadID        int                    `json:"tid"`
	Args            map[string]interface{} `json:"args"`
}

// completeEvent is the event phase name for complete events in trace.
//
// https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/edit#heading=h.puwqg050lyuy
const completeEvent = "X"

type traceByStart []Trace

func (t traceByStart) Len() int           { return len(t) }
func (t traceByStart) Swap(i, j int)      { t[i], t[j] = t[j], t[i] }
func (t traceByStart) Less(i, j int) bool { return t[i].TimestampMicros < t[j].TimestampMicros }

func toTrace(step Step, pid int, tid int) Trace {
	tr := Trace{
		Name:            step.Out,
		Category:        step.Category(),
		EventType:       completeEvent,
		TimestampMicros: int(step.Start / time.Microsecond),
		DurationMicros:  int(step.Duration() / time.Microsecond),
		ProcessID:       pid,
		ThreadID:        tid,
		Args:            make(map[string]interface{}),
	}
	if step.Command != nil {
		tr.Args["command"] = step.Command.Command
	}
	return tr
}

// ToTraces converts Flow outputs into trace log.
func ToTraces(steps [][]Step, pid int) []Trace {
	traceNum := 0
	for _, thread := range steps {
		traceNum += len(thread)
	}

	traces := make([]Trace, 0, traceNum)
	for tid, thread := range steps {
		for _, step := range thread {
			traces = append(traces, toTrace(step, pid, tid))
		}
	}
	sort.Sort(traceByStart(traces))
	return traces
}

// clangTrace matches the JSON output format from clang when time-trace is
// enabled.
type clangTrace struct {
	// TraceEvents contains all events in this trace.
	TraceEvents []Trace
	// BeginningOfTimeMicros identifies the time when this clang command started,
	// using microseconds since epoch.
	BeginningOfTimeMicros int `json:"beginningOfTime"`
}

// ClangTracesToInterleave returns all clang traces that can be interleaved
// directly into their corresponding build steps from `mainTraces`.
//
// `buildRoot` should point to the directory where the Ninja build of
// `mainTrace` is executed, where this function will look for clang traces next
// to object files built by clang. Object files with no clang traces next to
// them are skipped.
//
// Clang traces include events with very short durations, so `granularity` is
// provided to filter them and reduce the size of returned slice.
func ClangTracesToInterleave(mainTraces []Trace, buildRoot string, granularity time.Duration) ([]Trace, error) {
	var interleaved []Trace

	for _, mainTrace := range mainTraces {
		if !strings.HasSuffix(mainTrace.Name, ".o") {
			continue
		}

		// Clang writes a .json file next to the compiled object file when
		// time-trace is enabled.
		//
		// https://releases.llvm.org/9.0.0/tools/clang/docs/ReleaseNotes.html#new-compiler-flags
		clangTracePath := filepath.Join(buildRoot, strings.TrimSuffix(mainTrace.Name, ".o")+".json")
		if _, err := os.Stat(clangTracePath); err != nil {
			if os.IsNotExist(err) {
				continue
			}
			return nil, err
		}
		traceFile, err := os.Open(clangTracePath)
		if err != nil {
			return nil, err
		}
		var cTrace clangTrace
		if err := json.NewDecoder(traceFile).Decode(&cTrace); err != nil {
			return nil, fmt.Errorf("failed to decode clang trace %s: %w", clangTracePath, err)
		}

		for _, t := range cTrace.TraceEvents {
			// Event names starting with "Total" are sums of event durations of a
			// type, for example: "Total Frontend". They are used to form a bar chart
			// in clang traces, so they always stat from time 0 on separate threads.
			// We exclude them so they don't cause interleaved events to misalign.
			if strings.HasPrefix(t.Name, "Total ") || t.EventType != completeEvent || t.DurationMicros < int(granularity/time.Microsecond) {
				continue
			}

			if t.DurationMicros > mainTrace.DurationMicros {
				return nil, fmt.Errorf("clang trace for %q has an event %q with duration %dµs, which is longer than the duration of the this clang build %dµs, please make sure they are from the same build", mainTrace.Name, t.Name, t.DurationMicros, mainTrace.DurationMicros)
			}

			// Left align this event with the corresponding step in the main trace,
			// and put them in the same thread, so in the trace viewer it will be
			// displayed right below that step.
			t.ProcessID = mainTrace.ProcessID
			t.ThreadID = mainTrace.ThreadID
			t.TimestampMicros += mainTrace.TimestampMicros

			interleaved = append(interleaved, t)
		}
	}
	return interleaved, nil
}
