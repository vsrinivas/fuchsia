// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ninjalog

import (
	"sort"
)

// Trace is an entry of trace format.
// https://code.google.com/p/trace-viewer/
type Trace struct {
	Name      string                 `json:"name"`
	Category  string                 `json:"cat"`
	EventType string                 `json:"ph"`
	Timestamp int                    `json:"ts"`  // microsecond
	Duration  int                    `json:"dur"` // microsecond
	ProcessID int                    `json:"pid"`
	ThreadID  int                    `json:"tid"`
	Args      map[string]interface{} `json:"args"`
}

type traceByStart []Trace

func (t traceByStart) Len() int           { return len(t) }
func (t traceByStart) Swap(i, j int)      { t[i], t[j] = t[j], t[i] }
func (t traceByStart) Less(i, j int) bool { return t[i].Timestamp < t[j].Timestamp }

func toTrace(step Step, pid int, tid int) Trace {
	return Trace{
		Name:      step.Out,
		Category:  "target",
		EventType: "X",
		Timestamp: int(step.Start.Nanoseconds() / 1000),
		Duration:  int(step.Duration().Nanoseconds() / 1000),
		ProcessID: pid,
		ThreadID:  tid,
		Args:      make(map[string]interface{}),
	}
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
