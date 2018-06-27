// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"context"
)

// TriggerHandler is a visitor for Node types that can trigger actions
type TriggerContext struct {
	Source LineSource
	Mods   []Module
	Segs   []Segment
}

// TriggerTap is a nop on the pipeline that reads trigger information out
type TriggerTap struct {
	dumps    []*DumpfileElement
	handlers []func(*DumpfileElement)
}

func NewTriggerTap() *TriggerTap {
	return &TriggerTap{}
}

func (t *TriggerTap) AddHandler(handler func(*DumpfileElement)) {
	t.handlers = append(t.handlers, handler)
}

func (t *TriggerTap) Start(ctx context.Context, input <-chan OutputLine) <-chan OutputLine {
	out := make(chan OutputLine)
	go func() {
		defer func() {
			close(out)
		}()
		for {
			select {
			case <-ctx.Done():
				return
			case elem, ok := <-input:
				if !ok {
					return
				}
				for _, node := range elem.line {
					if dump, ok := node.(*DumpfileElement); ok {
						for _, handler := range t.handlers {
							handler(dump)
						}
					}
				}
				out <- elem
			}
		}
	}()
	return out
}
