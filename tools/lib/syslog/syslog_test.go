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
	"reflect"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
)

type fakeSSHRunner struct {
	pr          *io.PipeReader
	closed      bool
	reconnected bool
	errs        chan error
	lastCmd     []string
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
	r.reconnected = true
	return nil
}

func (r *fakeSSHRunner) Close() error {
	r.closed = true
	return nil
}

func incrementalStream(cmd []string) bool {
	if reflect.DeepEqual(cmd, []string{"/bin/log_listener"}) {
		return false
	} else if reflect.DeepEqual(cmd, []string{"/bin/log_listener", "--since_now", "yes"}) {
		return true
	} else {
		panic(fmt.Sprintf("unrecognized command %v", cmd))
	}
}

func TestStream(t *testing.T) {
	// NoLogLevel may be changed for verbosity while debugging.
	l := logger.NewLogger(logger.NoLogLevel, nil, nil, nil, "")
	ctx := logger.WithLogger(context.Background(), l)

	pr, pw := io.Pipe()
	r := &fakeSSHRunner{
		pr:   pr,
		errs: make(chan error),
	}
	defer pr.Close()
	defer pw.Close()

	syslogger := Syslogger{r: r}
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
			streamErrs <- syslogger.Stream(ctx, &buf, false)
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
		} else if incrementalStream(r.lastCmd) {
			t.Errorf("streaming should only be incremental after reconnection")
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
		} else if incrementalStream(r.lastCmd) {
			t.Errorf("streaming should only be incremental after reconnection")
		}
	})

	// Finally, if we come across a connection error we should reconnect.
	t.Run("stream should recover from a connection error", func(t *testing.T) {
		// After the first connection error, we should attempt to stream again,
		// at which point we have a stubbed nil error waiting to be consumed.
		// The streaming error returned should too be nil, by which point we should
		// have reconnected.
		r.errs <- &sshutil.ConnectionError{fmt.Errorf("connection error")}
		r.errs <- nil
		err := <-streamErrs
		if err != nil {
			t.Errorf("unexpected streaming error: %v", err)
		} else if !r.reconnected {
			t.Errorf("runner failed to reconnect")
		} else if !incrementalStream(r.lastCmd) {
			t.Errorf("streaming should be incremental after reconnection")
		}
	})
}
