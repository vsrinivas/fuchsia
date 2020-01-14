// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package serial

import (
	"context"
	"fmt"
	"io"
	"net"

	"go.fuchsia.dev/fuchsia/tools/lib/ring"
)

// Server proxies all i/o to/from a serial port via another io.ReadWriter.
// Start and Stop may be pairwise called any number of times.
type Server struct {
	serial          io.ReadWriter
	opts            ServerOptions
	done            chan struct{}
	writeBufferSize int
}

// ServerOptions provide options that parametrize the server's behavior.
type ServerOptions struct {
	// WriteBufferSize is the size of the write buffer of the socket connection
	// that is to be established.
	WriteBufferSize int

	// AuxiliaryOutput is an optional serial output sink.
	AuxiliaryOutput io.Writer
}

// NewServer returns a new server that lives atop the given 'serial' port.
func NewServer(serial io.ReadWriter, opts ServerOptions) *Server {
	if opts.WriteBufferSize <= 0 {
		panic(fmt.Sprintf("serial server: needs a positive write buffer to initialize: %d <= 0", opts.WriteBufferSize))
	}
	return &Server{
		serial: serial,
		opts:   opts,
		done:   make(chan struct{}, 1),
	}
}

// Run begins the server and blocks until the context signals done or an error
// is encountered. While running, all serial i/o is forwarded to and from the
// first connection accepted by the listener.
func (s *Server) Run(ctx context.Context, listener net.Listener) error {
	errs := make(chan error)

	// The data below flows as
	//
	// serial -> ring buffer -> conn
	//        -> aux output
	//
	// and back as
	//
	// conn -> serial
	//
	// We use an intermediate ring buffer so as not to block on writes to
	// the connection (as would happen in the case of a socket, for example):
	// this which would prevent timely writes to the auxiliary output.

	// serial -> (ring buffer, aux output)
	rb := ring.NewBuffer(s.opts.WriteBufferSize)
	go func() {
		var teedSerial io.Reader = s.serial
		if s.opts.AuxiliaryOutput != nil {
			teedSerial = io.TeeReader(s.serial, s.opts.AuxiliaryOutput)
		}
		for {
			if _, err := io.Copy(rb, teedSerial); err != nil {
				errs <- fmt.Errorf("failed to read from serial: %v", err)
			}
		}
	}()

	go func() {
		conn, err := listener.Accept()
		if err != nil {
			errs <- err
			return
		}
		defer conn.Close()

		// net.Conn does not feature a SetWriteBuffer method, but all of its
		// main net implementations do.
		wbs, ok := conn.(writeBufferSetter)
		if ok {
			if err := wbs.SetWriteBuffer(s.opts.WriteBufferSize); err != nil {
				errs <- err
				return
			}
		}

		// conn -> serial
		go func() {
			for {
				if _, err := io.Copy(s.serial, conn); err != nil {
					errs <- fmt.Errorf("failed to read from the connection: %v", err)
				}
			}
		}()

		// ring buffer -> conn
		for {
			if _, err := io.Copy(conn, rb); err != nil {
				errs <- fmt.Errorf("failed to copy data to the connection: %v", err)
			}
		}
	}()

	select {
	case <-ctx.Done():
		return nil
	case err := <-errs:
		return err
	}
}

type writeBufferSetter interface {
	SetWriteBuffer(int) error
}

