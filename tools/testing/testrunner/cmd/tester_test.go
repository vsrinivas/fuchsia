// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"reflect"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/integration/testsharder/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"

	"github.com/google/go-cmp/cmp"
	"golang.org/x/crypto/ssh"
)

type fakeRunner struct {
	reconnectIfNecessaryErrs  []error
	reconnectIfNecessaryCalls int
	runErrs                   []error
	runCalls                  int
	lastCmd                   []string
}

func (r *fakeRunner) Run(ctx context.Context, command []string, stdout, stderr io.Writer) error {
	r.runCalls++
	r.lastCmd = command
	if r.runErrs == nil {
		return nil
	}
	err, remainingErrs := r.runErrs[0], r.runErrs[1:]
	r.runErrs = remainingErrs
	return err
}

func (r *fakeRunner) Close() error {
	return nil
}

func (r *fakeRunner) ReconnectIfNecessary(ctx context.Context) (*ssh.Client, error) {
	r.reconnectIfNecessaryCalls++
	if r.reconnectIfNecessaryErrs == nil {
		return nil, nil
	}
	err, remainingErrs := r.reconnectIfNecessaryErrs[0], r.reconnectIfNecessaryErrs[1:]
	r.reconnectIfNecessaryErrs = remainingErrs
	return nil, err
}

func TestSubprocessTester(t *testing.T) {
	cases := []struct {
		name    string
		test    build.Test
		runErrs []error
		wantErr bool
		wantCmd []string
	}{
		{
			name:    "no command no path",
			test:    build.Test{},
			wantErr: true,
		},
		{
			name: "no command yes path",
			test: build.Test{
				Path: "/foo/bar",
			},
			wantErr: false,
			wantCmd: []string{"/foo/bar"},
		},
		{
			name: "command succeeds",
			test: build.Test{
				Command: []string{"foo", "bar"},
			},
			wantErr: false,
			wantCmd: []string{"foo", "bar"},
		},
		{
			name: "command fails",
			test: build.Test{
				Command: []string{"foo", "bar"},
			},
			runErrs: []error{fmt.Errorf("test failed")},
			wantErr: true,
			wantCmd: []string{"foo", "bar"},
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			runner := &fakeRunner{
				runErrs: c.runErrs,
			}
			tester := subprocessTester{
				r: runner,
			}
			defer func() {
				if err := tester.Close(); err != nil {
					t.Errorf("Close returned error: %v", err)
				}
			}()
			_, err := tester.Test(context.Background(), testsharder.Test{Test: c.test}, ioutil.Discard, ioutil.Discard)
			if gotErr := (err != nil); gotErr != c.wantErr {
				t.Errorf("tester.Test got error: %v, want error: %t", err, c.wantErr)
			}
			if diff := cmp.Diff(c.wantCmd, runner.lastCmd); diff != "" {
				t.Errorf("Unexpected command run (-want +got):\n%s", diff)
			}
		})
	}
}

type fakeDataSinkCopier struct{}

func (*fakeDataSinkCopier) GetReference() (runtests.DataSinkReference, error) {
	return runtests.DataSinkReference{}, nil
}

func (*fakeDataSinkCopier) Copy(sinks []runtests.DataSinkReference, localDir string) (runtests.DataSinkMap, error) {
	return runtests.DataSinkMap{}, nil
}

func (*fakeDataSinkCopier) Close() error {
	return nil
}

func TestSSHTester(t *testing.T) {
	cases := []struct {
		name        string
		runErrs     []error
		reconErrs   []error
		wantErr     bool
		wantConnErr bool
	}{
		{
			name:    "sucess",
			runErrs: []error{nil},
			wantErr: false,
		},
		{
			name:    "test failure",
			runErrs: []error{fmt.Errorf("test failed")},
			wantErr: true,
		},
		{
			name:      "connection error retry and test failure",
			runErrs:   []error{sshutil.ConnectionError{}, fmt.Errorf("test failed")},
			reconErrs: []error{nil},
			wantErr:   true,
		},
		{
			name:      "reconnect succeeds then fails",
			runErrs:   []error{sshutil.ConnectionError{}, sshutil.ConnectionError{}},
			reconErrs: []error{nil, fmt.Errorf("reconnect failed")},
			wantErr:   true,
			// Make sure we return the original ConnectionError and not the error from the failed
			// reconnect attempt. This is important because the code that calls Test() in a loop
			// aborts the loop when it sees an ConnectionError.
			wantConnErr: true,
		},
		{
			name:      "reconnect succeeds thrice",
			runErrs:   []error{sshutil.ConnectionError{}, sshutil.ConnectionError{}, sshutil.ConnectionError{}},
			reconErrs: []error{nil, nil, nil},
			wantErr:   true,
			// Reconnection succeeds so we don't want the caller to see a ConnectionError.
			wantConnErr: false,
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			runner := &fakeRunner{
				reconnectIfNecessaryErrs: c.reconErrs,
				runErrs:                  c.runErrs,
			}
			tester := fuchsiaSSHTester{
				r:                           runner,
				copier:                      &fakeDataSinkCopier{},
				connectionErrorRetryBackoff: &retry.ZeroBackoff{},
			}
			defer func() {
				if err := tester.Close(); err != nil {
					t.Errorf("Close returned error: %v", err)
				}
			}()
			wantReconnCalls := len(c.reconErrs)
			wantRunCalls := len(c.runErrs)
			_, err := tester.Test(context.Background(), testsharder.Test{}, ioutil.Discard, ioutil.Discard)
			if err == nil {
				if c.wantErr {
					t.Errorf("tester.Test got nil error, want non-nil error")
				}
			} else {
				if !c.wantErr {
					t.Errorf("tester.Test got error: %v, want nil", err)
				}
				if isConnErr := sshutil.IsConnectionError(err); isConnErr != c.wantConnErr {
					t.Errorf("got isConnErr: %t, want: %t", isConnErr, c.wantConnErr)
				}
			}
			if wantReconnCalls != runner.reconnectIfNecessaryCalls {
				t.Errorf("ReconnectIfNecesssary() called wrong number of times. Got: %d, Want: %d", runner.reconnectIfNecessaryCalls, wantReconnCalls)
			}
			if wantRunCalls != runner.runCalls {
				t.Errorf("Run() called wrong number of times. Got: %d, Want: %d", runner.runCalls, wantRunCalls)
			}
		})
	}
}

// Creates pair of ReadWriteClosers that mimics the relationship between serial
// and socket i/o. Implemented with in-memory pipes, the input of one can
// synchronously by read as the output of the other.
func serialAndSocket() (io.ReadWriteCloser, io.ReadWriteCloser) {
	rSerial, wSocket := io.Pipe()
	rSocket, wSerial := io.Pipe()
	serial := &joinedPipeEnds{rSerial, wSerial}
	socket := &joinedPipeEnds{rSocket, wSocket}
	return serial, socket
}

func TestSerialTester(t *testing.T) {
	ctx := context.Background()
	serial, socket := serialAndSocket()
	defer socket.Close()
	defer serial.Close()

	tester := fuchsiaSerialTester{socket: socket}
	test := testsharder.Test{
		Test: build.Test{
			Path: "foo",
		},
	}
	expectedCmd := "runtests --output /data/infra/testrunner foo\r\n"

	t.Run("test passes", func(t *testing.T) {
		errs := make(chan error)
		go func() {
			_, err := tester.Test(ctx, test, ioutil.Discard, ioutil.Discard)
			errs <- err
		}()

		// The write to the socket will block until we read from serial.
		buff := make([]byte, len(expectedCmd))
		if _, err := io.ReadFull(serial, buff); err != nil {
			t.Errorf("error reading from serial: %v", err)
		} else if string(buff) != expectedCmd {
			t.Errorf("unexpected command: %s", buff)
		}

		// At this point, the tester will be blocked reading from the socket.
		if _, err := io.WriteString(serial, runtests.SuccessSignature); err != nil {
			t.Errorf("failed to write %s to serial", runtests.SuccessSignature)
		}

		select {
		case err := <-errs:
			if err != nil {
				t.Error("test unexpectedly failed")
			}
		}
	})
	t.Run("test fails", func(t *testing.T) {
		errs := make(chan error)
		go func() {
			_, err := tester.Test(ctx, test, ioutil.Discard, ioutil.Discard)
			errs <- err
		}()
		// The write to the socket will block until we read from serial.
		buff := make([]byte, len(expectedCmd))
		if _, err := io.ReadFull(serial, buff); err != nil {
			t.Errorf("error reading from serial: %v", err)
		} else if string(buff) != expectedCmd {
			t.Errorf("unexpected command: %s", buff)
		}

		// At this point, the tester will be blocked reading from the socket.
		if _, err := io.WriteString(serial, runtests.FailureSignature); err != nil {
			t.Errorf("failed to write %s to serial", runtests.FailureSignature)
		}

		select {
		case err := <-errs:
			if err == nil {
				t.Error("test unexpectedly passed")
			}
		}
	})

}

func TestSetCommand(t *testing.T) {
	cases := []struct {
		name        string
		test        testsharder.Test
		useRuntests bool
		timeout     time.Duration
		expected    []string
	}{
		{
			name:        "specified command is respected",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:    "/path/to/test",
					Command: []string{"a", "b", "c"},
				}},
			expected: []string{"a", "b", "c"},
		},
		{
			name:        "use runtests URL",
			useRuntests: true,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cmx",
				}},
			expected: []string{"runtests", "--output", "REMOTE_DIR", "fuchsia-pkg://example.com/test.cmx"},
		},
		{
			name:        "use runtests path",
			useRuntests: true,
			test: testsharder.Test{
				Test: build.Test{
					Path: "/path/to/test",
				}},
			expected: []string{"runtests", "--output", "REMOTE_DIR", "/path/to/test"},
		},
		{
			name:        "use runtests timeout",
			useRuntests: true,
			test: testsharder.Test{
				Test: build.Test{
					Path: "/path/to/test",
				}},
			timeout:  time.Second,
			expected: []string{"runtests", "--output", "REMOTE_DIR", "-i", "1", "/path/to/test"},
		},
		{
			name:        "system path",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path: "/path/to/test",
				}},
			expected: []string{"/path/to/test"},
		},
		{
			name:        "components v1",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cmx",
				}},
			expected: []string{"run-test-component", "--restrict-logs", "fuchsia-pkg://example.com/test.cmx"},
		},
		{
			name:        "components v1 timeout",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cmx",
				}},
			timeout:  time.Second,
			expected: []string{"run-test-component", "--restrict-logs", "--timeout=1", "fuchsia-pkg://example.com/test.cmx"},
		},
		{
			name:        "components v2",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cm",
				}},
			expected: []string{"run-test-suite", "fuchsia-pkg://example.com/test.cm"},
		},
		{
			name:        "components v2 timeout",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cm",
				}},
			timeout:  time.Second,
			expected: []string{"run-test-suite", "--timeout", "1", "fuchsia-pkg://example.com/test.cm"},
		},
	}

	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			setCommand(&c.test, c.useRuntests, "REMOTE_DIR", c.timeout)
			if !reflect.DeepEqual(c.test.Command, c.expected) {
				t.Errorf("unexpected command:\nexpected: %q\nactual: %q\n", c.expected, c.test.Command)
			}
		})

	}
}

type joinedPipeEnds struct {
	r *io.PipeReader
	w *io.PipeWriter
}

func (pe *joinedPipeEnds) Read(p []byte) (int, error) {
	return pe.r.Read(p)
}

func (pe *joinedPipeEnds) Write(p []byte) (int, error) {
	return pe.w.Write(p)
}

func (pe *joinedPipeEnds) Close() error {
	if err := pe.r.Close(); err != nil {
		pe.w.Close()
		return err
	}
	return pe.w.Close()
}
