// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package syslog

import (
	"context"
	"errors"
	"io"
	"io/ioutil"
	"os"
	"runtime/pprof"
	"sync"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
)

const testTimeout = time.Second

type fakeSSHClient struct {
	// Run() will listen on this channel and return any error that it receives.
	mockRunErrs chan error
	// Run() will listen on this channel and send any received data to the
	// `stdout` writer.
	mockRunStdout chan string
	// ReconnectWithBackoff() will close this channel.
	reconnected chan struct{}

	// mu controls access to the `runListeners` field.
	mu sync.Mutex
	// Channel that will be closed next time Run() is called.
	runListener chan struct{}
}

func (c *fakeSSHClient) Run(ctx context.Context, _ []string, stdout, _ io.Writer) error {
	c.mu.Lock()
	if c.runListener != nil {
		close(c.runListener)
		c.runListener = nil
	}
	c.mu.Unlock()

	for {
		select {
		case mockStdout := <-c.mockRunStdout:
			stdout.Write([]byte(mockStdout))
			continue
		case err := <-c.mockRunErrs:
			return err
		case <-ctx.Done():
			return ctx.Err()
		}
	}
}

// addRunListener returns a channel that will be closed the next time
// client.Run() is called.
func (c *fakeSSHClient) listenForRun() <-chan struct{} {
	c.mu.Lock()
	if c.runListener == nil {
		c.runListener = make(chan struct{})
	}
	c.mu.Unlock()
	return c.runListener
}

func (c *fakeSSHClient) ReconnectWithBackoff(_ context.Context, _ retry.Backoff) error {
	close(c.reconnected)
	return nil
}

// startStream creates a new syslogger using a fake SSH client and starts
// streaming from that syslogger. It returns the fake SSH client used by the
// syslogger, an io.Reader from which the syslogs output by Stream() can be
// read, and a channel on which the error (possibly nil) returned by Stream()
// will be sent.
//
// Note that this function will only call Stream() once, with no retries,
// although Stream() itself is expected to have internal retries for certain
// failure modes.
func startStream(ctx context.Context) (*fakeSSHClient, io.Reader, <-chan error) {
	client := &fakeSSHClient{
		mockRunErrs:   make(chan error),
		mockRunStdout: make(chan string),
		reconnected:   make(chan struct{}),
	}
	running := client.listenForRun()

	syslogger := &Syslogger{
		client: client,
	}
	pipeReader, pipeWriter := io.Pipe()
	streamErrs := make(chan error, 1)

	// Start streaming in a background goroutine that should run for the
	// duration of the test that uses this function.
	go func() {
		streamErrs <- syslogger.Stream(ctx, pipeWriter)
	}()

	// Don't return until we've started running the SSH command, to ensure that
	// we always return a client in a deterministic state.
	<-running
	return client, pipeReader, streamErrs
}

// runWithTimeout runs the given function with `testTimeout`, and fails the test
// (and prints a stacktrace) if the function exceeds the timeout.
func runWithTimeout(t *testing.T, f func() error, failureMessage string) error {
	errs := make(chan error, 1)
	go func() {
		errs <- f()
	}()

	select {
	case err := <-errs:
		return err
	case <-time.After(testTimeout):
		pprof.Lookup("goroutine").WriteTo(os.Stderr, 1)
		t.Fatalf(failureMessage)
	}
	return nil
}

func assertClosed(t *testing.T, channel <-chan struct{}, failureMessage string) {
	runWithTimeout(t, func() error {
		<-channel
		return nil
	}, failureMessage)
}

func TestStream(t *testing.T) {
	// NoLogLevel may be changed for verbosity while debugging.
	l := logger.NewLogger(logger.NoLogLevel, nil, nil, nil, "")
	ctx := logger.WithLogger(context.Background(), l)

	t.Run("streams stdout until context canceled", func(t *testing.T) {
		ctx, cancel := context.WithCancel(ctx)
		defer cancel()
		client, streamOutput, streamErrs := startStream(ctx)

		stdout := "ABCDE"
		client.mockRunStdout <- stdout
		buf := make([]byte, len(stdout))
		io.ReadAtLeast(streamOutput, buf, len(stdout))
		if string(buf) != "ABCDE" {
			t.Errorf("unexpected bytes. wanted: %q, got: %q", stdout, string(buf))
		}

		runningAgain := client.listenForRun()

		cancel()

		err := runWithTimeout(t, func() error {
			return <-streamErrs
		}, "expected context cancellation to stop streaming")
		if !errors.Is(err, context.Canceled) {
			t.Fatalf("unexpected streaming error: %v", err)
		}

		select {
		case <-runningAgain:
			t.Errorf("expected client.Run() to only be called once")
		default:
		}

		select {
		case <-client.reconnected:
			t.Errorf("client unexpectedly reconnected")
		default:
		}
	})

	// Errors not of type sshutil.ConnectionError should cause the syslogger to
	// exit, rather than reconnecting and resuming streaming.
	t.Run("non-connection error interrupts the stream", func(t *testing.T) {
		client, _, streamErrs := startStream(ctx)

		// We'll be notified in case Run() is called again (it shouldn't be
		// called again).
		runningAgain := client.listenForRun()

		// Run() will return a non-connection error, which should cause Stream()
		// to exit.
		nonConnectionError := errors.New("foo")
		client.mockRunErrs <- nonConnectionError

		err := runWithTimeout(t, func() error {
			return <-streamErrs
		}, "expected a Run() error to stop streaming")
		if !errors.Is(err, nonConnectionError) {
			t.Fatalf("unexpected streaming error: %v, expected: %v", err, nonConnectionError)
		}

		// Stream() should have exited immediately after receiving the error
		// from client.Run(), so it should not have called Run() again.
		select {
		case <-runningAgain:
			t.Errorf("expected client.Run() to be called once")
		default:
		}

		// Likewise, we shouldn't have tried to reconnect after receiving the
		// error from client.Run(), because the error was not SSH-related.
		select {
		case <-client.reconnected:
			t.Errorf("runner unexpectedly reconnected")
		default:
		}
	})

	// If we come across a connection error we should reconnect and re-run log_listener.
	t.Run("stream should recover from a connection error", func(t *testing.T) {
		client, streamOutput, _ := startStream(ctx)

		runningAgain := client.listenForRun()

		go func() {
			// Read output so that the syslogger doesn't get blocked on writing
			// the "syslog stream interrupted" message.
			ioutil.ReadAll(streamOutput)
		}()

		client.mockRunErrs <- sshutil.ConnectionError{}

		assertClosed(t, client.reconnected,
			"expected syslogger to reconnect after connection error, but it didn't")

		assertClosed(t, runningAgain,
			"expected syslogger to re-run log_listener after reconnection")
	})

	// If log_listener exits successfully, we should just keep running it.
	t.Run("stream should rerun log_listener if it exits successfully", func(t *testing.T) {
		client, _, _ := startStream(ctx)

		runningAgain := client.listenForRun()

		client.mockRunErrs <- nil

		assertClosed(t, runningAgain,
			"expected syslogger to re-run log_listener again after a successful exit")

		select {
		case <-client.reconnected:
			t.Errorf("runner unexpectedly reconnected")
		default:
		}
	})
}
