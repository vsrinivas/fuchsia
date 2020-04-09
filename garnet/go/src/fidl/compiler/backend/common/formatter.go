// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package common

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"os/exec"
	"time"
)

// Formatter formats a writer stream.
type Formatter struct {
	path string
	args []string
}

const timeout = 2 * time.Minute

// NewFormatter creates a new formatter.
//
// The `path` needs to either
// * Point to an executable which formats stdin and outputs it to stdout;
// * An empty string, in which case no formatting will occur.
func NewFormatter(path string, args ...string) Formatter {
	return Formatter{
		path: path,
		args: args,
	}
}

// FormatPipe formats an output stream.
//
// When the returned WriteCloser is closed, 'out' will also be closed.
// This allows the caller to close the writer in a single location and received all relevant errors.
func (f Formatter) FormatPipe(out io.WriteCloser) (io.WriteCloser, error) {
	if f.path == "" {
		return unformattedStream{normalOut: out}, nil
	}
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	cmd := exec.CommandContext(ctx, f.path, f.args...)
	cmd.Stdout = out
	errBuf := new(bytes.Buffer)
	cmd.Stderr = errBuf
	in, err := cmd.StdinPipe()
	if err != nil {
		cancel()
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		cancel()
		return nil, err
	}
	return formattedStream{
		cmd:    cmd,
		cancel: cancel,
		in:     in,
		out:    out,
		errBuf: errBuf,
	}, nil
}

type unformattedStream struct {
	normalOut io.Writer
}

type formattedStream struct {
	cmd    *exec.Cmd
	cancel context.CancelFunc
	in     io.WriteCloser
	out    io.WriteCloser
	errBuf *bytes.Buffer
}

var _ = []io.WriteCloser{
	unformattedStream{},
	formattedStream{},
}

func (s unformattedStream) Write(p []byte) (int, error) {
	return s.normalOut.Write(p)
}

func (s formattedStream) Write(p []byte) (int, error) {
	return s.in.Write(p)
}

func (s unformattedStream) Close() error {
	// Not the responsibility of unformattedStream to close underlying stream
	// which may (or may not) be an io.WriteCloser.
	return nil
}

func (s formattedStream) Close() error {
	defer s.cancel()
	if err := s.in.Close(); err != nil {
		return err
	}
	if err := s.cmd.Wait(); err != nil {
		if errContent := s.errBuf.Bytes(); len(errContent) != 0 {
			return fmt.Errorf("%s: %s", err, string(errContent))
		}
		return err
	}
	return s.out.Close()
}
