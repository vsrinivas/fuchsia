// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"context"
)

// PostProcessor is the type of modifications to a stream of OutputLines
type PostProcessor interface {
	Process(OutputLine, chan<- OutputLine)
}

// PreProcessor is the type of modifications to a stream of InputLines
type PreProcessor interface {
	Process(InputLine, chan<- InputLine)
}

// Processor is the type of transformations from OutputLines to InputLines
type Processor interface {
	Process(InputLine, chan<- OutputLine)
}

type postProcessorNode struct {
	postProc PostProcessor
}

func (p postProcessorNode) run(ctx context.Context, input <-chan OutputLine) <-chan OutputLine {
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
				p.postProc.Process(elem, out)
			}
		}
	}()
	return out
}

type preProcessorNode struct {
	preProc PreProcessor
}

func (p preProcessorNode) run(ctx context.Context, input <-chan InputLine) <-chan InputLine {
	out := make(chan InputLine)
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
				p.preProc.Process(elem, out)
			}
		}
	}()
	return out
}

type processorNode struct {
	proc Processor
}

func (p processorNode) run(ctx context.Context, input <-chan InputLine) <-chan OutputLine {
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
				p.proc.Process(elem, out)
			}
		}
	}()
	return out
}

// Consume eats and forgets all input from a channel. This is useful for keeping a goroutine alive
// until a pipeline has finished processing to completion.
func Consume(input <-chan OutputLine) {
	for range input {
	}
}

// ComposePreProcessors takes several PreProcessors and runs them in sequence
func ComposePreProcessors(ctx context.Context, input <-chan InputLine, pres ...PreProcessor) <-chan InputLine {
	for _, pre := range pres {
		input = preProcessorNode{pre}.run(ctx, input)
	}
	return input
}

// ComposePostProcessors takes several PostProcessors and runs them in sequence
func ComposePostProcessors(ctx context.Context, input <-chan OutputLine, posts ...PostProcessor) <-chan OutputLine {
	for _, post := range posts {
		input = postProcessorNode{post}.run(ctx, input)
	}
	return input
}

// Compose takes a single Processor and causes it to start transforming Input into Output
func ComposeProcessors(ctx context.Context, input <-chan InputLine, proc Processor) <-chan OutputLine {
	return processorNode{proc}.run(ctx, input)
}
