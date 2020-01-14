// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package serial

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/hex"
	"io"
	"net"
	"os"
	"path/filepath"
	"testing"
)

const (
	writeBufferSize = 1024
)

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

func socketPath() string {
	// We randomly construct a socket path that is highly improbable to collide with anything.
	randBytes := make([]byte, 16)
	rand.Read(randBytes)
	return filepath.Join(os.TempDir(), "serial"+hex.EncodeToString(randBytes)+".sock")
}

func TestServer(t *testing.T) {
	serial, device := serialAndDevice()

	defer func() {
		serial.Close()
		device.Close()
	}()

	s := NewServer(serial, ServerOptions{
		WriteBufferSize: writeBufferSize,
	})

	path := socketPath()
	addr := &net.UnixAddr{Name: path, Net: "unix"}
	l, err := net.ListenUnix("unix", addr)
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()

	ctx, cancel := context.WithCancel(context.Background())

	// Run the server in the main routine and the tests in a separate routine so
	// that we can actually test the shutdown functionality of the server (by the
	// termination of this program).
	go testServer(t, s, device, path, nil, cancel)
	if err := s.Run(ctx, l); err != nil {
		t.Fatalf("server encountered shutdown error: %v", err)
	}
}

func TestServerWithAuxOutput(t *testing.T) {
	serial, device := serialAndDevice()
	auxOutputReader, auxOutput := io.Pipe()

	defer func() {
		serial.Close()
		device.Close()
		auxOutput.Close()
		auxOutputReader.Close()
	}()

	s := NewServer(serial, ServerOptions{
		WriteBufferSize: writeBufferSize,
		AuxiliaryOutput: auxOutput,
	})

	path := socketPath()
	addr := &net.UnixAddr{Name: path, Net: "unix"}
	l, err := net.ListenUnix("unix", addr)
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()

	ctx, cancel := context.WithCancel(context.Background())

	// Run the server in the main routine and the tests in a separate routine so
	// that we can actually test the shutdown functionality of the server (by the
	// termination of this program).
	go testServer(t, s, device, path, auxOutputReader, cancel)
	if err := s.Run(ctx, l); err != nil {
		t.Fatalf("server encountered shutdown error: %v", err)
	}
}

func testServer(t *testing.T, s *Server, device io.ReadWriter, socketPath string, auxOutputReader io.Reader, cancel context.CancelFunc) {
	t.Helper()

	var socket net.Conn
	for {
		var err error
		socket, err = net.Dial("unix", socketPath)
		if err != nil {
			t.Logf("failed to open client socket connection: %v", err)
			continue
		}
		break
	}
	defer socket.Close()

	//
	// X -> device -> serial -> socket -> Y
	//
	// Make the device "write to serial" on its side. This should unblock the
	// reads being made from serial on the host side and forwarded to the
	// socket. Reads from the socket are in turn blocking, so we should be able
	// to reliably verify that X == Y.
	//
	t.Run("writes to serial can be read from socket", func(t *testing.T) {
		input := []byte("written to serial!")
		if _, err := device.Write(input); err != nil {
			t.Fatalf("failed to write to device: %v", err)
		}

		if auxOutputReader != nil {
			// Read first from the auxiliary output, as the teeing logic from serial should
			// block until we read from the other end of the pipe.
			readAndCheckBytes(t, auxOutputReader, "teed output", input)
		}
		readAndCheckBytes(t, socket, "socket", input)
	})

	//
	// X -> socket -> serial -> device -> Y
	//
	// Have the device "read from serial" on its side after bytes have been
	// written to the socket. This read will block until the bytes have been
	// forwarded from the server socket to serial on the host side, which will
	// in turn unblock the initial read. This is all to say that this case too
	// should be a reliable verification that X == Y.
	//
	t.Run("writes to socket can be read from serial", func(t *testing.T) {
		input := []byte("written to the socket!")
		if _, err := socket.Write(input); err != nil {
			t.Fatalf("failed to write to socket: %v", err)
		}
		readAndCheckBytes(t, device, "serial", input)
	})

	t.Run("server shuts down", func(t *testing.T) {
		cancel()
	})
}

func readAndCheckBytes(t *testing.T, r io.Reader, readerName string, expected []byte) {
	t.Helper()
	p := make([]byte, writeBufferSize)
	n, err := r.Read(p)
	if (err != nil && err != io.EOF) || n == 0 {
		t.Fatalf("failed to read from %s: %v", readerName, err)
	}
	actual := p[0:n]
	if bytes.Compare(expected, actual) != 0 {
		t.Errorf("unexpected bytes read from %s:\nexpected: %q\nactual: %q", readerName, expected, actual)
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