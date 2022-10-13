// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"sync"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/lib/subprocess"
)

type failRunner struct {
	stdout string
	stderr string
	err    string
}

func (f *failRunner) Run(ctx context.Context, command []string, options subprocess.RunOptions) error {
	io.WriteString(options.Stdout, f.stdout)
	io.WriteString(options.Stderr, f.stderr)
	return fmt.Errorf("%s", f.err)
}

func TestExpectedStdout(t *testing.T) {
	// Test that on error we get the expected Stdout result
	var stderr bytes.Buffer
	var stdout bytes.Buffer
	ctx := context.Background()
	br := newBatchRunner(ctx, &failRunner{"foo", "bar", "baz"}, 1)
	br.Enqueue([]string{}, &stdout, &stderr)
	batchErr := br.Wait()
	if batchErr == nil {
		t.Error("expected non-nil error on Wait but got nil")
	}
	if stdout.String() != "foo" {
		t.Error("expected stdout to be \"foo\" but got ", stdout.String())
	}
	if stderr.String() != "bar" {
		t.Error("expected stderr to be \"bar\" but got ", stderr.String())
	}
	if batchErr.Error() != "baz" {
		t.Error("expected batchErr to be \"baz\" but got ", batchErr.Error())
	}
}

type ThreadSafeBuffer struct {
	buf  bytes.Buffer
	lock sync.Mutex
}

func (t *ThreadSafeBuffer) Write(data []byte) (int, error) {
	defer t.lock.Unlock()
	t.lock.Lock()
	return t.buf.Write(data)
}

func (t *ThreadSafeBuffer) String() string {
	return t.buf.String()
}

type mockRunner struct{}

func (m mockRunner) Run(ctx context.Context, command []string, options subprocess.RunOptions) error {
	for _, str := range command {
		io.WriteString(options.Stderr, str)
	}
	return nil
}

func TestEnqueueWait(t *testing.T) {
	// Do a standard smoke test of the basic functionality.
	var stderr ThreadSafeBuffer
	var stdout bytes.Buffer
	ctx := context.Background()
	// Set max batch size of 3
	br := newBatchRunner(ctx, mockRunner{}, 3)
	// Enqueue more than 3 tasks.
	for i := 0; i < 10; i++ {
		br.Enqueue([]string{"a"}, &stdout, &stderr)
	}
	if err := br.Wait(); err != nil {
		t.Error(err)
	}
	// This is also how I feel inside.
	expected := "aaaaaaaaaa"
	if stderr.String() != expected {
		t.Error("expected stderr to be \""+expected+"\" but got ", stderr.String())
	}
}

type ignoreWriter struct{}

func (i ignoreWriter) Write(data []byte) (int, error) {
	return len(data), nil
}

func TestEnqueuePanic(t *testing.T) {
	// Test that Enqueue panics if called after Wait.
	defer func() {
		if r := recover(); r == nil {
			t.Error("expected panic but got none")
		}
	}()
	ctx := context.Background()
	br := newBatchRunner(ctx, mockRunner{}, 1)
	if err := br.Wait(); err != nil {
		t.Error(err)
	}
	br.Enqueue([]string{}, ignoreWriter{}, ignoreWriter{})
}

type loopRunner struct{}

func (m loopRunner) Run(ctx context.Context, _ []string, _ subprocess.RunOptions) error {
	<-ctx.Done()
	return nil
}

func TestCancelJob(t *testing.T) {
	// Test that canceling a job unblocks it.
	var stderr bytes.Buffer
	var stdout bytes.Buffer
	ctx, cancel := context.WithCancel(context.Background())
	br := newBatchRunner(ctx, loopRunner{}, 2)
	br.Enqueue([]string{}, &stdout, &stderr)
	cancel()
	if err := br.Wait(); err != nil {
		t.Error(err)
	}
	if len(stderr.Bytes()) != 0 {
		t.Error("expected no output on stderr but got: ", stderr.String())
	}
	if len(stdout.Bytes()) != 0 {
		t.Error("expected no output on stdout but got: ", stdout.String())
	}
}
