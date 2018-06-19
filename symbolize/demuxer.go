// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"context"
	"fmt"

	"fuchsia.googlesource.com/tools/logger"
)

type remuxer struct {
	seq chan (<-chan OutputLine)
	out chan<- OutputLine
}

func newRemuxer() *remuxer {
	return &remuxer{seq: make(chan (<-chan OutputLine), 1024)}
}

func (r *remuxer) sequence(in <-chan OutputLine) {
	r.seq <- in
}

func (r *remuxer) start(ctx context.Context) (<-chan OutputLine, error) {
	if r.out != nil {
		return nil, fmt.Errorf("Attempt to start an already running remuxer")
	}
	out := make(chan OutputLine)
	r.out = out
	go func() {
		defer close(r.out)
		for {
			select {
			case <-ctx.Done():
				return
			case in, ok := <-r.seq:
				if !ok {
					return
				}
				r.out <- (<-in)
			}
		}
	}()
	return out, nil
}

func (r *remuxer) stop() error {
	close(r.seq)
	if r.seq == nil {
		return fmt.Errorf("Attempt to stop a remuxer that hasn't been started")
	}
	return nil
}

type pipe struct {
	in  chan<- InputLine
	out <-chan OutputLine
}

// Demuxer demultiplexes incomming input lines, spins up new filters, and sends them input lines.
type Demuxer struct {
	// Spin up new filters as new procsses are found.
	filters map[lineSource]pipe
	// Use same symbolizer for all
	symbolizer Symbolizer
	// Use same repo for all
	repo *SymbolizerRepo
}

// NewDemuxer creates a new demuxer.
func NewDemuxer(repo *SymbolizerRepo, symbo Symbolizer) *Demuxer {
	return &Demuxer{
		repo:       repo,
		symbolizer: symbo,
		filters:    make(map[lineSource]pipe),
	}
}

// Start tells the demuxer to start consuming input and dispatching to the filters.
func (d *Demuxer) Start(ctx context.Context, input <-chan InputLine) <-chan OutputLine {
	// Create the remuxer.
	remux := newRemuxer()
	out, err := remux.start(ctx)
	if err != nil {
		logger.Fatalf(ctx, "Failed to start remuxer in case where that should never happen.")
	}
	go func() {
		// Clean up the channels/goroutines when we're done
		defer func() {
			for _, pipe := range d.filters {
				close(pipe.in)
			}
			err := remux.stop()
			if err != nil {
				logger.Warningf(ctx, "%v", err)
			}
		}()
		// Start multiplexing things out
		for {
			select {
			case <-ctx.Done():
				return
			case line, ok := <-input:
				if !ok {
					return
				}
				var fpipe pipe
				if fpipe, ok = d.filters[line.source]; !ok {
					fin := make(chan InputLine)
					// Spin up a new Filter for this process.
					filt := NewFilter(d.repo, d.symbolizer)
					fout := filt.Start(ctx, fin)
					fpipe = pipe{in: fin, out: fout}
					d.filters[line.source] = fpipe
				}
				// sequence a read from fpipe.out
				remux.sequence(fpipe.out)
				fpipe.in <- line
			}
		}
	}()
	return out
}
