// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serial

import (
	"bufio"
	"context"
	"errors"
	"io"
	"io/ioutil"
	"log"
	"net"
	"os"
	"time"
)

// ErrNetClosing comes from the stdlib, but is not exported by the stdlib, see
// https://golang.org/issues/4373. There is a test to assert this is still a
// sound match for the behavior we're matching against.
var ErrNetClosing = errors.New("use of closed network connection")

func IsErrNetClosing(err error) bool {
	if e, ok := err.(*net.OpError); ok {
		return e.Err.Error() == ErrNetClosing.Error()
	}
	return false
}

// Server proxies all i/o to/from a serial port via another io.ReadWriter.
// Start and Stop may be pairwise called any number of times.
type Server struct {
	ServerOptions
	serial io.ReadWriteCloser
}

// ServerOptions provide options that parametrize the server's behavior.
type ServerOptions struct {
	// Logger if set to a non-nil value will receive log output for internal
	// errors that are incidental to program logic, but may be relevant for
	// diagnosis of general behaviors, such as errors in IO with the unix socket
	// clients.
	Logger *log.Logger

	// AuxiliaryOutput is an optional serial output sink. It will be closed before
	// server.Run returns. It is dup'd for each incoming socket.
	AuxiliaryOutput *os.File

	// StartAtEnd instructs each connection to begin streaming at the end
	// of the aux file.
	StartAtEnd bool
}

// NewServer returns a new server that lives atop the given 'serial' port.
func NewServer(serial io.ReadWriteCloser, opts ServerOptions) *Server {
	s := &Server{
		ServerOptions: opts,
		serial:        serial,
	}
	return s
}

// Run begins the server and blocks until the context signals done or an error
// is encountered reading from serial. While running, all serial i/o is
// forwarded to and from the any connection accepted by the listener.
// The listener is closed by the time Run returns.
func (s *Server) Run(ctx context.Context, listener net.Listener) error {
	ctx, cancel := context.WithCancel(ctx)

	if s.AuxiliaryOutput == nil {
		f, err := ioutil.TempFile("", "tools-serial-aux-output")
		if err != nil {
			return err
		}
		s.AuxiliaryOutput = f
		defer os.Remove(s.AuxiliaryOutput.Name())
	}

	go func() {
		for {
			conn, err := listener.Accept()
			if err != nil {
				// if the listener was closed, we don't care.
				if IsErrNetClosing(err) {
					return
				}
				s.logf("error: serial: accept: %s", err)
				return
			}

			// Start a loop that reads whole lines from conn, and writes whole lines
			// to the serial port. This is not complete, it would be better to have a
			// proper line discipline for the serial port, however there are multiple
			// writers to the port, this being one of them. We can best-effort
			// non-conflicting writes by at least attempting to write lines as an
			// atomic unit, though this is still fallible.
			go func() {
				b := bufio.NewReader(conn)
				for {
					l, err := b.ReadString('\n')
					if err != nil {
						s.logf("error: serial: conn read: %s", err)
						return
					}
					_, err = io.WriteString(s.serial, l)
					if err != nil {
						s.logf("error: serial: write: %s", err)
						return
					}
				}
			}()

			go func() {
				defer conn.Close()

				f, err := os.Open(s.AuxiliaryOutput.Name())
				if err != nil {
					s.logf("error: serial: %s", err)
				}
				defer f.Close()

				if s.StartAtEnd {
					_, err = f.Seek(0, io.SeekEnd)
					if err != nil {
						s.logf("error: serial: seek: %s", err)
						return
					}
				}

				// keep copying until conn is closed
				buf := make([]byte, 4096)
				for {
					// files on unix always return readable, even if select'd so
					// there's no great strategy for "tailing". It's possible to relax
					// some of this to a stat loop to check for appends, and/or to use
					// filesystem watcher APIs, but in this context, this rate is both
					// easy enough on CPU and fast enough for keeping up with serial.
					select {
					case <-ctx.Done():
						return
					case <-time.After(50 * time.Millisecond):
						_, err := io.CopyBuffer(conn, f, buf)
						// io.Copy returns nil on io.EOF, which is useful here, as that's the
						// case where we read the end of the file
						if err == nil {
							continue
						}
						return
					}
				}
			}()
		}
	}()

	errs := make(chan error)
	go func() {
		buf := make([]byte, 4096)
		for {
			_, err := io.CopyBuffer(s.AuxiliaryOutput, s.serial, buf)
			if err != nil {
				errs <- err
				return
			}
		}
	}()

	var err error
	select {
	case <-ctx.Done():
		// close the serial port and wait for the copy goroutine to finish, so that
		// we are confident that auxoutput has received all of the data up to the
		// close.
		s.serial.Close()
		<-errs
	case err = <-errs:
		s.serial.Close()
		cancel()
	}

	listener.Close()
	return err
}

func (s *Server) logf(format string, args ...interface{}) {
	if s.Logger != nil {
		s.Logger.Printf(format, args...)
	}
}
