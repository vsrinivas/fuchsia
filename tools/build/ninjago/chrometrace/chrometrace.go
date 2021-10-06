// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// package chrometrace contains utilities for working with Chrome traces.
package chrometrace

// Trace is an entry of trace format.
//
// https://code.google.com/p/trace-viewer/
type Trace struct {
	Name            string                 `json:"name"`
	Category        string                 `json:"cat"`
	EventType       string                 `json:"ph"`
	TimestampMicros int                    `json:"ts"`
	DurationMicros  int                    `json:"dur"`
	ProcessID       int                    `json:"pid"`
	ThreadID        int                    `json:"tid"`
	Args            map[string]interface{} `json:"args,omitempty"`
	ID              int                    `json:"id,omitempty"`
	BindingPoint    string                 `json:"bp,omitempty"`
}

// Event types used in conversions.
//
// https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/edit#heading=h.puwqg050lyuy
const (
	CompleteEvent  = "X"
	FlowEventStart = "s"
	FlowEventEnd   = "f"
)

// ByStart is a wrapper type around a slice of Traces ordered by event start time.
//
// This type implements sort.Interface, see https://pkg.go.dev/sort#Interface.
type ByStart []Trace

func (t ByStart) Len() int           { return len(t) }
func (t ByStart) Swap(i, j int)      { t[i], t[j] = t[j], t[i] }
func (t ByStart) Less(i, j int) bool { return t[i].TimestampMicros < t[j].TimestampMicros }
