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
	"log"

	"io/ioutil"
	"net"
	"os"
	"testing"
)

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

	aux, err := ioutil.TempFile("", "test-serial-aux")
	if err != nil {
		t.Fatal(err)
	}

	s := NewServer(serial, ServerOptions{AuxiliaryOutput: aux})

	ctx, cancel := context.WithCancel(context.Background())

	var serverErr error
	serverDone := make(chan struct{})
	go func() {
		l, _ := testListener(t)
		serverErr = s.Run(ctx, l)
		close(serverDone)
	}()

	buf := make([]byte, 1024*1024)
	_, err = io.ReadFull(rand.Reader, buf)
	if err != nil {
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

	aux, err := ioutil.TempFile("", "test-serial-aux")
	if err != nil {
		t.Fatal(err)
	}

	s := NewServer(serial, ServerOptions{AuxiliaryOutput: aux, Logger: log.New(os.Stdout, "test-serial", log.LstdFlags)})

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
	go func() {
		for i := 0; i < 10000; i++ {
			if _, err := io.WriteString(device, "hello world\n"); err != nil {
				t.Fatal(err)
			}
		}
		if _, err := io.WriteString(device, "MAGIC!\n"); err != nil {
			t.Fatal(err)
		}
	}()

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
}

func TestServerSerialWrites(t *testing.T) {
	serial, device := serialAndDevice()

	aux, err := ioutil.TempFile("", "test-serial-aux")
	if err != nil {
		t.Fatal(err)
	}

	s := NewServer(serial, ServerOptions{AuxiliaryOutput: aux, Logger: log.New(os.Stdout, "test-serial", log.LstdFlags)})

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
	// A test driver writes things to conn
	go func() {
		for i := 0; i < expectedCount-1; i++ {
			if _, err := io.WriteString(c, "hello world\n"); err != nil {
				t.Fatal(err)
			}
		}
		if _, err := io.WriteString(c, "MAGIC!\n"); err != nil {
			t.Fatal(err)
		}
	}()

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
}

func TestServerSerialClosing(t *testing.T) {
	serial, _ := serialAndDevice()

	aux, err := ioutil.TempFile("", "test-serial-aux")
	if err != nil {
		t.Fatal(err)
	}

	s := NewServer(serial, ServerOptions{AuxiliaryOutput: aux, Logger: log.New(os.Stdout, "test-serial", log.LstdFlags)})

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

func TestIsErrNetClosing(t *testing.T) {
	// see golang issue 4373, this is the sad story from upstream, and the pattern
	// we follow is similar to that of the net/http and net/http2 package. There's
	// a variable stashed away in internal/poll, but it's not actually exported to
	// us for comparison.

	l, err := net.ListenUnix("unix", &net.UnixAddr{Net: "unix", Name: "foo.sock"})
	if err != nil {
		t.Fatal(err)
	}
	l.Close()
	_, err = l.Accept()

	if !IsErrNetClosing(err) {
		t.Fatalf("expected a wrapped errnetclosing, got: %#v", err)
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

	f, err := ioutil.TempFile("", "test-serial-listener")
	if err != nil {
		t.Fatal(err)
	}
	f.Close()
	os.Remove(f.Name())
	l, err := net.ListenUnix("unix", &net.UnixAddr{Name: f.Name(), Net: "unix"})
	if err != nil {
		t.Fatal(err)
	}
	l.SetUnlinkOnClose(true)
	return l, f.Name()
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
