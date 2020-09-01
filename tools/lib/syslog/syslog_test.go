// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package syslog

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
)

type fakeSSHClient struct {
	pr          *io.PipeReader
	closed      bool
	reconnected bool
	errs        chan error
	lastCmd     []string
}

func (c *fakeSSHClient) Run(_ context.Context, cmd []string, stdout, _ io.Writer) error {
	c.lastCmd = cmd
	copyErrs := make(chan error)
	go func() {
		// We won't "stream" large amounts of data in these tests.
		buf := make([]byte, 64)

		n, err := c.pr.Read(buf)
		if err != nil {
			copyErrs <- err
			return
		}
		_, err = stdout.Write(buf[:n])
		copyErrs <- err
	}()

	select {
	case err := <-c.errs:
		return err
	case err := <-copyErrs:
		return err
	}
}

func (c *fakeSSHClient) ReconnectWithBackoff(_ context.Context, _ retry.Backoff) error {
	c.reconnected = true
	return nil
}

func (c *fakeSSHClient) Close() {
	c.closed = true
}

func TestStream(t *testing.T) {
	// NoLogLevel may be changed for verbosity while debugging.
	l := logger.NewLogger(logger.NoLogLevel, nil, nil, nil, "")
	ctx := logger.WithLogger(context.Background(), l)

	pr, pw := io.Pipe()
	defer pr.Close()
	defer pw.Close()
	c := &fakeSSHClient{
		pr:   pr,
		errs: make(chan error),
	}

	syslogger := Syslogger{c}

	var buf bytes.Buffer
	streamErrs := make(chan error)
	go func() {
		for {
			streamErrs <- syslogger.Stream(ctx, &buf)
		}
	}()

	// The streaming will block until we stub in an error or bytes to stream.
	// First let's test our abstraction to verify that bytes are indeed streamed.
	t.Run("streams if no errors are encountered", func(t *testing.T) {
		io.WriteString(pw, "ABCDE")
		err := <-streamErrs
		if err != nil {
			t.Fatalf("unexpected streaming error: %v", err)
		}
		b, _ := ioutil.ReadAll(&buf)
		if string(b) != "ABCDE" {
			t.Errorf("unexpected bytes:\nexpected: ABCDE\nactual: %s\n", b)
		} else if c.reconnected {
			t.Errorf("runner unexpectedly reconnected")
		}
	})

	// Now we check that errors not of type sshutil.ConnectionError will not
	// effect a reconnection.
	t.Run("non-connection error interrupts the stream", func(t *testing.T) {
		c.errs <- fmt.Errorf("error unrelated to connection")
		err := <-streamErrs
		if err == nil {
			t.Fatalf("expected a streaming failure")
		}
		b, _ := ioutil.ReadAll(&buf)
		if len(b) > 0 {
			t.Errorf("unexpected bytes streamed: %q", b)
		} else if c.reconnected {
			t.Errorf("runner unexpectedly reconnected")
		}
	})

	// If we come across a connection error we should reconnect.
	t.Run("stream should recover from a connection error", func(t *testing.T) {
		// After the first connection error, we should attempt to stream again,
		// at which point we have a stubbed nil error waiting to be consumed.
		// The streaming error returned should too be nil, by which point we should
		// have reconnected.
		c.errs <- sshutil.ConnectionError{}
		c.errs <- nil
		err := <-streamErrs
		if err != nil {
			t.Errorf("unexpected streaming error: %v", err)
		} else if !c.reconnected {
			t.Errorf("runner failed to reconnect")
		}
	})
}
