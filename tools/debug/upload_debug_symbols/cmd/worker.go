// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"log"
	"sync"
	"time"
)

type jobTimeout struct {
	job     job
	timeout time.Duration
}

func (t jobTimeout) Error() string {
	return fmt.Sprintf("job %s timed out after %.1fs", t.job.name, timeout.Seconds())
}

type withTimeoutFunc func(context.Context, time.Duration) (context.Context, context.CancelFunc)

// worker processes all jobs on the input channel, emitting any errors on errs.
func worker(ctx context.Context, bkt bucket, wg *sync.WaitGroup, withTimeout withTimeoutFunc, timeout time.Duration, jobs <-chan job, errs chan<- error, uploadPaths chan<- string) {
	defer wg.Done()
	for job := range jobs {
		ctx, cancel := withTimeout(ctx, timeout)
		defer cancel()
		jobErrs := make(chan error)
		log.Printf("executing %s", job.name)
		go func() {
			jobErrs <- job.ensure(ctx, bkt)
		}()
		select {
		case err := <-jobErrs:
			if err != nil {
				errs <- fmt.Errorf("job %s failed: %v", job.name, err)
			} else {
				uploadPaths <- job.gcsPath
			}
		case <-ctx.Done():
			errs <- jobTimeout{job: job, timeout: timeout}
		}
	}
}
