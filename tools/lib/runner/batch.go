// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package runner

import (
	"context"
	"io"
	"sync"
)

// Runner defines the interface for running commands by many means such as via SSH
// or via Shell or serial or some other such means.
type Runner interface {
	Run(context.Context, []string, io.Writer, io.Writer) error
}

// BatchRunner allows many tasks to be run in paralell using a Runner.
// BatchRunner will give every process the same stderr of your choice
// but will save the contents of every stdout for later debugging.
type BatchRunner struct {
	ctx        context.Context
	cancel     func()
	wg         sync.WaitGroup
	canEnqueue chan struct{}
	runner     Runner
	errs       chan error
}

// NewBatchRunner creates a BatchRunner that can use runner to run many jobs
// in paralell. At most maxBatchSize jobs will be active at once. Enqueue will
// block when maxBatchSize is exceeded. When ctx is Done, all jobs will terminate
// without error.
func NewBatchRunner(ctx context.Context, runner Runner, maxBatchSize int) *BatchRunner {
	ctxCancel, cancel := context.WithCancel(ctx)
	return &BatchRunner{
		ctx:        ctxCancel,
		cancel:     cancel,
		canEnqueue: make(chan struct{}, maxBatchSize),
		runner:     runner,
		errs:       make(chan error, 1),
	}
}

// Enqueue is similair to Run in Runner except that it's async. The call returns
// immediately and the enqueued command starts running. If however the maximum
// batch size has been reached Enqueue will block until a new job opens up.
func (b *BatchRunner) Enqueue(command []string, stdout, stderr io.Writer, closers ...func()) {
	// Note that the order of the wg.Add and the canEnqueue send below is important.
	// We'd like the property to hold that if any part of BatchRunner's Wait method
	// has executed then no matter where we are in this function, we either panic or
	// function correctly (e.g. the job is enqueued and will be waited on). If we flip
	// the order of wg.Add and the canEnqueue send there are interleavings that
	// allow Wait and Enqueue to race such that Wait returns before an Enqueued job
	// completes yet Enqueue doesn't panic.
	b.wg.Add(1)
	// This dynamically prevents Enqueue being run after Wait and sets the maximum batch
	// size.
	b.canEnqueue <- struct{}{}
	go func() {
		defer func() {
			b.wg.Done()
			// This should always succeed instantly.
			<-b.canEnqueue
			for _, closer := range closers {
				closer()
			}
		}()
		// Now this goroutine blocks until Run finishes.
		if err := b.runner.Run(b.ctx, command, stdout, stderr); err != nil {
			// If an error has already been sent out don't bother sending another.
			select {
			case b.errs <- err:
				// Stop all other jobs.
				b.cancel()
				return
			default:
				return
			}
		}
	}()
}

// Wait blocks on all previouslly Enqueued tasks to finish. It is invalid for
// Enqueue to be called after Wait is called.
func (b *BatchRunner) Wait() error {
	close(b.canEnqueue)
	b.wg.Wait()
	select {
	case err := <-b.errs:
		return err
	default:
		return nil
	}
}
