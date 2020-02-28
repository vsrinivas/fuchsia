// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package syslog

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
)

type fakeSSHRunner struct {
	pr            *io.PipeReader
	closed        bool
	reconnected   bool
	errs          chan error
	reconnectErrs chan error
	lastCmd       []string
}

func (r *fakeSSHRunner) Run(_ context.Context, cmd []string, stdout, _ io.Writer) error {
	r.lastCmd = cmd
	copyErrs := make(chan error)
	go func() {
		// We won't "stream" large amounts of data in these tests.
		buf := make([]byte, 64)

		n, err := r.pr.Read(buf)
		if err != nil {
			copyErrs <- err
			return
		}
		_, err = stdout.Write(buf[:n])
		copyErrs <- err
	}()

	select {
	case err := <-r.errs:
		return err
	case err := <-copyErrs:
		return err
	}
}

func (r *fakeSSHRunner) Reconnect(_ context.Context) error {
	select {
	case err := <-r.reconnectErrs:
		if err != nil {
			return err
		}
	default:
		// If the channel is empty, we consider reconnection to be successful.
	}
	r.reconnected = true
	return nil
}

func (r *fakeSSHRunner) Close() error {
	r.closed = true
	return nil
}

func TestStream(t *testing.T) {
	// NoLogLevel may be changed for verbosity while debugging.
	l := logger.NewLogger(logger.NoLogLevel, nil, nil, nil, "")
	ctx := logger.WithLogger(context.Background(), l)

	pr, pw := io.Pipe()
	defer pr.Close()
	defer pw.Close()
	r := &fakeSSHRunner{
		pr:            pr,
		errs:          make(chan error),
		reconnectErrs: make(chan error, 10),
	}

	// Use a reconnectInterval of zero to avoid sleeping.
	syslogger := Syslogger{r: r, reconnectInterval: 0}
	defer func() {
		syslogger.Close()
		if !r.closed {
			panic("runner was not closed")
		}
	}()

	var buf bytes.Buffer
	streamErrs := make(chan error)
	go func() {
		for {
			streamErrs <- syslogger.Stream(ctx, &buf)
		}
	}()

	// The streaming will block until we stub in an error or bytes to stream.
	// First let's test our abstraction to verify that bytes are indeed streamed.
	t.Run("streams if no errors are encoutered", func(t *testing.T) {
		io.WriteString(pw, "ABCDE")
		err := <-streamErrs
		if err != nil {
			t.Fatalf("unexpected streaming error: %v", err)
		}
		b, _ := ioutil.ReadAll(&buf)
		if string(b) != "ABCDE" {
			t.Errorf("unexpected bytes:\nexpected: ABCDE\nactual: %s\n", b)
		} else if r.reconnected {
			t.Errorf("runner unexpectedly reconnected")
		}
	})

	// Now we check that errors not of type *sshutil.ConnectionError will not
	// effect a reconnection.
	t.Run("non-connection error interrupts the stream", func(t *testing.T) {
		r.errs <- fmt.Errorf("error unrelated to connection")
		err := <-streamErrs
		if err == nil {
			t.Fatalf("expected a streaming failure")
		}
		b, _ := ioutil.ReadAll(&buf)
		if len(b) > 0 {
			t.Errorf("unexpected bytes streamed: %q", b)
		} else if r.reconnected {
			t.Errorf("runner unexpectedly reconnected")
		}
	})

	// If we come across a connection error we should reconnect.
	t.Run("stream should recover from a connection error", func(t *testing.T) {
		// After the first connection error, we should attempt to stream again,
		// at which point we have a stubbed nil error waiting to be consumed.
		// The streaming error returned should too be nil, by which point we should
		// have reconnected.
		r.errs <- sshutil.ConnectionError
		r.errs <- nil
		err := <-streamErrs
		if err != nil {
			t.Errorf("unexpected streaming error: %v", err)
		} else if !r.reconnected {
			t.Errorf("runner failed to reconnect")
		}
	})

	// If reconnection fails, we should keep trying to reconnect.
	t.Run("stream should retry reconnection", func(t *testing.T) {
		// After a connection error, we should keep trying to reconnect until we
		// succeed.
		reconnectErr := errors.New("failed to reconnect")
		// Send N reconnection errors into the channel so that they get picked
		// up when the syslogger tries to reconnect after being interrupted by
		// the ConnectionError. This will cause Reconnect() to fail N times and
		// succeed the N+1th time.
		r.reconnectErrs <- reconnectErr
		r.reconnectErrs <- reconnectErr
		r.reconnectErrs <- reconnectErr
		r.errs <- sshutil.ConnectionError
		// Reconnection failures should not have caused `Stream()` to return, so
		// the streamErrs channel should be empty.
		select {
		case err := <-streamErrs:
			if err != nil {
				t.Errorf("stream didn't keep trying to reconnect: %v", err)
			} else {
				t.Errorf("unexpected completion of streaming after reconnection failure")
			}
		default:
		}
	})
}
