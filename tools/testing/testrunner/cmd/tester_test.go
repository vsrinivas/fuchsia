// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"math/rand"
	"net"
	"os"
	"path/filepath"
	"reflect"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/integration/testsharder"
	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/sshutil"
	"go.fuchsia.dev/fuchsia/tools/testing/runtests"
	"golang.org/x/sync/errgroup"

	"github.com/google/go-cmp/cmp"
)

type fakeSSHClient struct {
	reconnectErrs  []error
	reconnectCalls int
	runErrs        []error
	runCalls       int
	lastCmd        []string
}

func (c *fakeSSHClient) Run(_ context.Context, command []string, _, _ io.Writer) error {
	c.runCalls++
	c.lastCmd = command
	if c.runErrs == nil {
		return nil
	}
	err, remainingErrs := c.runErrs[0], c.runErrs[1:]
	c.runErrs = remainingErrs
	return err
}

func (c *fakeSSHClient) Close() {}

func (c *fakeSSHClient) Reconnect(_ context.Context) error {
	c.reconnectCalls++
	if c.reconnectErrs == nil {
		return nil
	}
	err, remainingErrs := c.reconnectErrs[0], c.reconnectErrs[1:]
	c.reconnectErrs = remainingErrs
	return err
}

type fakeCmdRunner struct {
	runErrs  []error
	runCalls int
	lastCmd  []string
}

func (r *fakeCmdRunner) Run(_ context.Context, command []string, _, _ io.Writer) error {
	r.runCalls++
	r.lastCmd = command
	if r.runErrs == nil {
		return nil
	}
	err, remainingErrs := r.runErrs[0], r.runErrs[1:]
	r.runErrs = remainingErrs
	return err
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
			name:    "no path",
			test:    build.Test{},
			wantErr: true,
		},
		{
			name: "yes path",
			test: build.Test{
				Path: "/foo/bar",
			},
			wantErr: false,
			wantCmd: []string{"/foo/bar"},
		},
		{
			name:    "test fails",
			test:    build.Test{Path: "/fail"},
			runErrs: []error{fmt.Errorf("test failed")},
			wantErr: true,
			wantCmd: []string{"/fail"},
		},
	}
	baseOutDir, err := ioutil.TempDir("", "TestSubprocessTester")
	if err != nil {
		t.Fatal("failed to create baseOutDir:", err)
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			runner := &fakeCmdRunner{
				runErrs: c.runErrs,
			}
			newRunner = func(dir string, env []string) cmdRunner {
				return runner
			}
			tester := subprocessTester{}
			defer func() {
				if err := tester.Close(); err != nil {
					t.Errorf("Close returned error: %v", err)
				}
			}()
			outDir := filepath.Join(baseOutDir, c.test.Path)
			_, err := tester.Test(context.Background(), testsharder.Test{Test: c.test}, ioutil.Discard, ioutil.Discard, outDir)
			if gotErr := (err != nil); gotErr != c.wantErr {
				t.Errorf("tester.Test got error: %v, want error: %t", err, c.wantErr)
			}
			if err == nil {
				if _, statErr := os.Stat(outDir); statErr != nil {
					t.Error("tester.Test did not create a readable outDir:", statErr)
				}
			}
			if diff := cmp.Diff(c.wantCmd, runner.lastCmd); diff != "" {
				t.Errorf("Unexpected command run (-want +got):\n%s", diff)
			}
		})
	}
}

type fakeDataSinkCopier struct {
	reconnectCalls int
}

func (*fakeDataSinkCopier) GetReference() (runtests.DataSinkReference, error) {
	return runtests.DataSinkReference{}, nil
}

func (*fakeDataSinkCopier) Copy(_ []runtests.DataSinkReference, _ string) (runtests.DataSinkMap, error) {
	return runtests.DataSinkMap{}, nil
}

func (c *fakeDataSinkCopier) Reconnect() error {
	c.reconnectCalls++
	return nil
}

func (*fakeDataSinkCopier) Close() error {
	return nil
}

func TestSSHTester(t *testing.T) {
	cases := []struct {
		name            string
		runErrs         []error
		reconErrs       []error
		copierReconErrs []error
		wantErr         bool
		wantConnErr     bool
		runSnapshot     bool
	}{
		{
			name:    "success",
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
		{
			name:        "reconnect before snapshot",
			runErrs:     []error{nil, sshutil.ConnectionError{}, nil},
			reconErrs:   []error{nil},
			wantErr:     false,
			wantConnErr: false,
			runSnapshot: true,
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			client := &fakeSSHClient{
				reconnectErrs: c.reconErrs,
				runErrs:       c.runErrs,
			}
			copier := &fakeDataSinkCopier{}
			tester := fuchsiaSSHTester{
				client:                      client,
				copier:                      copier,
				connectionErrorRetryBackoff: &retry.ZeroBackoff{},
			}
			eg := errgroup.Group{}
			serialServer := fakeSerialServer{
				received:       make([]byte, 1024),
				shutdownString: "shutdown",
				socketPath:     fmt.Sprintf("%d.sock", rand.Uint32()),
				listeningChan:  make(chan bool),
			}
			if c.wantConnErr {
				// This is a hack to ensure the shutdown command gets sent to the serial server.
				// Rather than introduce a new synchronization mechanism, just use the code under test's
				// existing mechanism for sending commands.
				serialDiagnosticCmds = append(serialDiagnosticCmds, []string{serialServer.shutdownString})
				// Ensure we don't waste time sleeping in this test.
				diagnosticToggleDuration = time.Microsecond
				tester.serialSocketPath = serialServer.socketPath
				defer os.Remove(serialServer.socketPath)
				eg.Go(serialServer.Serve)
				if !<-serialServer.listeningChan {
					t.Fatalf("fakeSerialServer.Serve() returned: %v", eg.Wait())
				}
			}
			defer func() {
				if err := tester.Close(); err != nil {
					t.Errorf("Close returned error: %v", err)
				}
			}()
			wantReconnCalls := len(c.reconErrs)
			wantRunCalls := len(c.runErrs)
			test := testsharder.Test{
				Test:         build.Test{PackageURL: "fuchsia-pkg://foo"},
				Runs:         1,
				RunAlgorithm: testsharder.StopOnSuccess,
			}
			_, err := tester.Test(context.Background(), test, ioutil.Discard, ioutil.Discard, "unused-out-dir")
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

			if c.runSnapshot {
				snapshotFile, err := ioutil.TempFile("", "snapshot")
				if err != nil {
					t.Fatal("TempFile() failed:", err)
				}
				defer func() {
					if err := os.Remove(snapshotFile.Name()); err != nil {
						t.Errorf("os.Remove(%s) failed: %v", snapshotFile.Name(), err)
					}
				}()
				err = tester.RunSnapshot(context.Background(), snapshotFile.Name())
				if err != nil {
					t.Errorf("failed to run snapshot: %v", err)
				}
			}

			if wantReconnCalls != client.reconnectCalls {
				t.Errorf("Reconnect() called wrong number of times. Got: %d, Want: %d", client.reconnectCalls, wantReconnCalls)
			}

			reconnFailures := 0
			for _, err := range c.reconErrs {
				if err != nil {
					reconnFailures++
				}
			}
			// The copier shouldn't be reconnected if reconnecting the ssh
			// client fails.
			wantCopierReconnCalls := wantReconnCalls - reconnFailures
			if wantCopierReconnCalls != copier.reconnectCalls {
				t.Errorf("Reconnect() called wrong number of times. Got: %d, Want: %d", copier.reconnectCalls, wantReconnCalls)
			}

			if wantRunCalls != client.runCalls {
				t.Errorf("Run() called wrong number of times. Got: %d, Want: %d", client.runCalls, wantRunCalls)
			}

			if c.wantConnErr {
				if err = eg.Wait(); err != nil {
					t.Errorf("serialServer.Serve() failed: %v", err)
				}
				// Ignore the shutdown command we appended at the end
				for _, cmd := range serialDiagnosticCmds[:len(serialDiagnosticCmds)-2] {
					firstIndex := bytes.Index(serialServer.received, []byte(asSerialCmd(cmd)))
					if firstIndex == -1 {
						t.Errorf("SSHTester did not execute command over serial: %v", cmd)
					}
					secondIndex := bytes.Index(serialServer.received[firstIndex+1:], []byte(asSerialCmd(cmd)))
					if secondIndex == -1 {
						t.Errorf("SSHTester only executed command over serial once, expected twice: %v, all received: %s", cmd, string(serialServer.received))
					}
				}
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

type fakeSerialServer struct {
	received       []byte
	shutdownString string
	socketPath     string
	listeningChan  chan bool
}

func (s *fakeSerialServer) Serve() error {
	listener, err := net.Listen("unix", s.socketPath)
	if err != nil {
		s.listeningChan <- false
		return fmt.Errorf("Listen(%s) failed: %v", s.socketPath, err)
	}
	defer listener.Close()
	s.listeningChan <- true
	conn, err := listener.Accept()
	if err != nil {
		return fmt.Errorf("Accept() failed: %v", err)
	}
	defer conn.Close()
	// Simulate booting.
	if _, err := conn.Write([]byte(serialConsoleCursor)); err != nil {
		return fmt.Errorf("conn.Write() failed: %v", err)
	}
	reader := iomisc.NewSequenceMatchingReader(conn, s.shutdownString)
	for {
		buf := make([]byte, 1024)
		bytesRead, err := reader.Read(buf)
		s.received = append(s.received, buf[:bytesRead]...)
		if err != nil {
			if err == io.EOF {
				return nil
			}
			return fmt.Errorf("conn.Read() failed: %v", err)
		}
	}
}

func TestNewSerialSocket(t *testing.T) {
	socketPath := fmt.Sprintf("%d.sock", rand.Uint32())
	defer os.Remove(socketPath)
	server := fakeSerialServer{
		received:       make([]byte, 1024),
		shutdownString: "dm shutdown",
		socketPath:     socketPath,
		listeningChan:  make(chan bool),
	}
	eg := errgroup.Group{}
	eg.Go(server.Serve)

	if !<-server.listeningChan {
		t.Fatalf("fakeSerialServer.Serve() returned: %v", eg.Wait())
	}

	clientSocket, err := newSerialSocket(context.Background(), socketPath)
	if err != nil {
		t.Fatalf("newSerialSocket() failed: %v", err)
	}
	bytesWritten, err := clientSocket.Write([]byte(server.shutdownString))
	if err != nil {
		t.Errorf("clientSocket.Write() failed: %v", err)
	}
	if bytesWritten != len(server.shutdownString) {
		t.Errorf("clientSocket.Write() wrote %d bytes, wanted %d", bytesWritten, len(server.shutdownString))
	}
	if err = eg.Wait(); err != nil {
		t.Errorf("server returned: %v", err)
	}
	if err = clientSocket.Close(); err != nil {
		t.Errorf("clientSocket.Close() returned: %v", err)
	}
}

func TestSerialTester(t *testing.T) {
	ctx := context.Background()
	serial, socket := serialAndSocket()
	defer socket.Close()
	defer serial.Close()

	tester := fuchsiaSerialTester{socket: socket}
	test := testsharder.Test{
		Test: build.Test{
			Name: "myfoo",
			Path: "foo",
		},
	}
	expectedCmd := "runtests --output /data/infra/testrunner foo\r\n"

	t.Run("test passes", func(t *testing.T) {
		errs := make(chan error)
		go func() {
			_, err := tester.Test(ctx, test, ioutil.Discard, ioutil.Discard, "unused-out-dir")
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
		successReturn := runtests.SuccessSignature + " " + test.Name
		if _, err := io.WriteString(serial, successReturn); err != nil {
			t.Errorf("failed to write %s to serial", successReturn)
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
			_, err := tester.Test(ctx, test, ioutil.Discard, ioutil.Discard, "unused-out-dir")
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
		failureReturn := runtests.FailureSignature + " " + test.Name
		if _, err := io.WriteString(serial, failureReturn); err != nil {
			t.Errorf("failed to write %s to serial", failureReturn)
		}

		select {
		case err := <-errs:
			if err == nil {
				t.Error("test unexpectedly passed")
			}
		}
	})

}

func TestCommandForTest(t *testing.T) {
	cases := []struct {
		name        string
		test        testsharder.Test
		useRuntests bool
		timeout     time.Duration
		expected    []string
		wantErr     bool
	}{
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
			wantErr: true,
		},
		{
			name:        "components v1",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cmx",
				}},
			expected: []string{"run-test-component", "fuchsia-pkg://example.com/test.cmx"},
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
			expected: []string{"run-test-component", "--timeout=1", "fuchsia-pkg://example.com/test.cmx"},
		},
		{
			name:        "components v1 max severity",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:        "/path/to/test",
					PackageURL:  "fuchsia-pkg://example.com/test.cmx",
					LogSettings: build.LogSettings{MaxSeverity: "ERROR"},
				}},
			timeout:  time.Second,
			expected: []string{"run-test-component", "--max-log-severity=ERROR", "--timeout=1", "fuchsia-pkg://example.com/test.cmx"},
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
			name:        "components v2 no parallel",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cm",
				}},
			expected: []string{"run-test-suite", "fuchsia-pkg://example.com/test.cm"},
		},
		{
			name:        "components v2 parallel",
			useRuntests: false,
			test: testsharder.Test{
				Test: build.Test{
					Path:       "/path/to/test",
					PackageURL: "fuchsia-pkg://example.com/test.cm",
					Parallel:   2,
				}},
			expected: []string{"run-test-suite", "--parallel", "2", "fuchsia-pkg://example.com/test.cm"},
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
			command, err := commandForTest(&c.test, c.useRuntests, "REMOTE_DIR", c.timeout)
			if err == nil {
				if c.wantErr {
					t.Errorf("commandForTest returned nil error, want non-nil error")
				}
			} else if !c.wantErr {
				t.Errorf("commandForTest returned error: %v, want nil", err)
			}
			if !reflect.DeepEqual(command, c.expected) {
				t.Errorf("unexpected command:\nexpected: %q\nactual: %q\n", c.expected, command)
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
