// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

// TriggerHandler is a visitor for Node types that can trigger actions
type TriggerContext struct {
	Source LineSource
	Mods   []Module
	Segs   []Segment
}

// TriggerTap is a nop on the pipeline that reads trigger information out
type TriggerTap struct {
	handlers []func(*DumpfileElement)
}

func NewTriggerTap() *TriggerTap {
	return &TriggerTap{}
}

func (t *TriggerTap) AddHandler(handler func(*DumpfileElement)) {
	t.handlers = append(t.handlers, handler)
}

func (t *TriggerTap) Process(line OutputLine, out chan<- OutputLine) {
	for _, node := range line.line {
		if dumpElem, ok := node.(*DumpfileElement); ok {
			for _, handler := range t.handlers {
				handler(dumpElem)
			}
		}
	}
	out <- line
}
