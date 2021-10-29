// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serial

import (
	"bufio"
	"bytes"
	"context"
	"crypto/rand"
	"io"
	"path/filepath"

	"io/ioutil"
	"net"
	"os"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/lib/color"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"golang.org/x/sync/errgroup"
)

var warningLogger = logger.NewLogger(logger.WarningLevel, color.NewColor(color.ColorNever), nil, nil, "test-serial")

func TestServerDrainsSerial(t *testing.T) {
	serial, device := serialAndDevice()

	s := NewServer(serial, ServerOptions{})

	ctx, cancel := context.WithCancel(context.Background())

	var serverErr error
	serverDone := make(chan struct{})
	go func() {
		l, _ := testListener(t)
		serverErr = s.Run(ctx, l)
		close(serverDone)
	}()

	// copy 1mb of random output to the server, if there were general IO problems
	// we'd likely block at around 64kb, this should shake out any common issues
	// with the normal copy pipeline
	_, err := io.CopyN(device, rand.Reader, 1024*1024)
	if err != nil {
		t.Fatal(err)
	}

	cancel()
	<-serverDone
	if serverErr != nil {
		t.Fatalf("unexpected server error: %s", serverErr)
	}
}

func TestServerAuxOutput(t *testing.T) {
	serial, device := serialAndDevice()
	aux := mkTempFile(t)

	s := NewServer(serial, ServerOptions{AuxiliaryOutput: aux.Name()})

	ctx, cancel := context.WithCancel(context.Background())

	var serverErr error
	serverDone := make(chan struct{})
	go func() {
		l, _ := testListener(t)
		serverErr = s.Run(ctx, l)
		close(serverDone)
	}()

	buf := make([]byte, 1024*1024)
	if _, err := io.ReadFull(rand.Reader, buf); err != nil {
		t.Fatal(err)
	}
	n, err := device.Write(buf)
	if err != nil || n != len(buf) {
		t.Fatalf("device write short or error: %s (%d/%d)", err, n, len(buf))
	}

	cancel()
	<-serverDone
	if serverErr != nil {
		t.Fatalf("unexpected server error: %s", serverErr)
	}
	if _, err := aux.Seek(0, io.SeekStart); err != nil {
		t.Fatal(err)
	}
	gotBuf, err := ioutil.ReadAll(aux)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(gotBuf, buf) {
		t.Fatalf("got\n%x\nwant\n%x", gotBuf, buf)
	}
}

func TestServerSocketOutput(t *testing.T) {
	serial, device := serialAndDevice()
	aux := mkTempFile(t)

	s := NewServer(serial, ServerOptions{AuxiliaryOutput: aux.Name()})

	ctx, cancel := context.WithCancel(context.Background())

	l, sockPath := testListener(t)

	var serverErr error
	serverDone := make(chan struct{})
	go func() {
		serverErr = s.Run(ctx, l)
		close(serverDone)
	}()

	c, err := net.DialUnix("unix", nil, &net.UnixAddr{Net: "unix", Name: sockPath})
	if err != nil {
		t.Fatal(err)
	}

	// A device writes things to serial, eventually writing a completion match the
	// reader was looking for
	var eg errgroup.Group
	eg.Go(func() error {
		for i := 0; i < 10000; i++ {
			if _, err := io.WriteString(device, "hello world\n"); err != nil {
				return err
			}
		}
		if _, err := io.WriteString(device, "MAGIC!\n"); err != nil {
			return err
		}
		return nil
	})

	// a client is tailing the log and tells the server it's done when it reads the magic:
	bio := bufio.NewReader(c)
	for {
		l, err := bio.ReadString('\n')
		if err != nil {
			t.Fatal(err)
		}
		if l == "MAGIC!\n" {
			break
		}
	}

	cancel()
	<-serverDone
	if serverErr != nil {
		t.Fatalf("unexpected server error: %s", serverErr)
	}

	if err := eg.Wait(); err != nil {
		t.Fatal(err)
	}
}

func TestServerSerialWrites(t *testing.T) {
	serial, device := serialAndDevice()
	aux := mkTempFile(t)

	s := NewServer(serial, ServerOptions{AuxiliaryOutput: aux.Name()})

	ctx, cancel := context.WithCancel(context.Background())

	l, sockPath := testListener(t)

	var serverErr error
	serverDone := make(chan struct{})
	go func() {
		serverErr = s.Run(ctx, l)
		close(serverDone)
	}()

	c, err := net.DialUnix("unix", nil, &net.UnixAddr{Net: "unix", Name: sockPath})
	if err != nil {
		t.Fatal(err)
	}

	var expectedCount = 10000
	var eg errgroup.Group
	// A test driver writes things to conn
	eg.Go(func() error {
		for i := 0; i < expectedCount-1; i++ {
			if _, err := io.WriteString(c, "hello world\n"); err != nil {
				return err
			}
		}
		if _, err := io.WriteString(c, "MAGIC!\n"); err != nil {
			return err
		}
		return nil
	})

	var count int
	// a device is receiving the input from the conn
	bio := bufio.NewReader(device)
	for {
		count++
		l, err := bio.ReadString('\n')
		if err != nil {
			t.Fatal(err)
		}
		if l == "MAGIC!\n" {
			break
		}
	}

	cancel()
	<-serverDone
	if serverErr != nil {
		t.Fatalf("unexpected server error: %s", serverErr)
	}

	if expectedCount != count {
		t.Fatalf("got %d, want %d", count, expectedCount)
	}

	if err := eg.Wait(); err != nil {
		t.Fatal(err)
	}
}

func TestServerSerialClosing(t *testing.T) {
	serial, _ := serialAndDevice()
	aux := mkTempFile(t)

	s := NewServer(serial, ServerOptions{AuxiliaryOutput: aux.Name()})

	ctx := context.Background()

	l, sockPath := testListener(t)

	var serverErr error
	serverDone := make(chan struct{})
	go func() {
		serverErr = s.Run(ctx, l)
		close(serverDone)
	}()

	c, err := net.DialUnix("unix", nil, &net.UnixAddr{Net: "unix", Name: sockPath})
	if err != nil {
		t.Fatal(err)
	}

	// serial goes away, say someone pulls the plug
	serial.Close()

	// the connection should get closed
	io.Copy(ioutil.Discard, c)

	// the server should exit cleanly
	<-serverDone
	// and it returns the underlying io error
	if serverErr == nil {
		t.Fatalf("server failed to return expected error")
	}
}

// Creates a "serial connection" in terms of its host- and device-side
// descriptors. They are implemented with synchronous in-memory pipes, so
// associated writes and reads will be one-to-one (with the usual caveats of
// io.Pipe).
func serialAndDevice() (io.ReadWriteCloser, io.ReadWriteCloser) {
	rs, wd := io.Pipe()
	rd, ws := io.Pipe()
	serial := &joinedPipeEnds{rs, ws}
	device := &joinedPipeEnds{rd, wd}
	return serial, device
}

func testListener(t *testing.T) (net.Listener, string) {
	t.Helper()

	name := filepath.Join(t.TempDir(), "test-serial-listener")
	l, err := net.ListenUnix("unix", &net.UnixAddr{Name: name, Net: "unix"})
	if err != nil {
		t.Fatal(err)
	}
	l.SetUnlinkOnClose(true)
	return l, name
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

// mkTempFile returns a new temporary file that will be cleaned up
// automatically.
func mkTempFile(t *testing.T) *os.File {
	f, err := os.Create(filepath.Join(t.TempDir(), "serial-test"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err := f.Close(); err != nil {
			t.Error(err)
		}
	})
	return f
}
