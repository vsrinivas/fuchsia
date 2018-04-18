// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"context"
	"sync"
)

// Demuxer demultiplexes incomming input lines, spins up new filters, and sends them input lines.
type Demuxer struct {
	// Spin up new filters as new procsses are found.
	filters map[uint64]chan<- InputLine
	// Use same symbolizer for all
	symbolizer Symbolizer
	// Use same repo for all
	repo *SymbolizerRepo
	// We need a wait group
	wgroup sync.WaitGroup
}

// NewDemuxer creates a new demuxer.
func NewDemuxer(repo *SymbolizerRepo, symbo Symbolizer) *Demuxer {
	return &Demuxer{
		repo:       repo,
		symbolizer: symbo,
		filters:    make(map[uint64]chan<- InputLine),
	}
}

// Start tells the demuxer to start consuming input and dispatching to the filters.
func (d *Demuxer) Start(input <-chan InputLine, pctx context.Context) <-chan OutputLine {
	var lineno uint64
	out := make(chan OutputLine)
	ctx, _ := context.WithCancel(pctx)
	go func() {
		//dd Clean up the channels/goroutines when we're done
		defer func() {
			for _, toFilter := range d.filters {
				close(toFilter)
			}
			d.wgroup.Wait()
			close(out)
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
				line.lineno = lineno
				lineno += 1
				if toFilter, ok := d.filters[line.process]; ok {
					// Send the input to the approprite filter.
					toFilter <- line
				} else {
					toFilter := make(chan InputLine)
					// Spin up a new Filter for this process.
					NewFilter(d.repo, d.symbolizer).Start(toFilter, out, &d.wgroup, ctx)
					d.filters[line.process] = toFilter
					toFilter <- line
				}
			}
		}
	}()
	return out
}
