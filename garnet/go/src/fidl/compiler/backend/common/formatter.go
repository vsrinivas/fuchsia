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
// If there is a error during formatting, the unformatted input will be written and an error will
// be returned.
//
// When the returned WriteCloser is closed, 'out' will also be closed.
// This allows the caller to close the writer in a single location and received all relevant errors.
func (f Formatter) FormatPipe(out io.WriteCloser) (io.WriteCloser, error) {
	if f.path == "" {
		return unformattedStream{normalOut: out}, nil
	}
	return formattedStream{
		path:           f.path,
		args:           f.args,
		out:            out,
		unformattedBuf: new(bytes.Buffer),
	}, nil
}

var _ = []io.WriteCloser{
	unformattedStream{},
	formattedStream{},
}

type unformattedStream struct {
	normalOut io.Writer
}

func (s unformattedStream) Write(p []byte) (int, error) {
	return s.normalOut.Write(p)
}

func (s unformattedStream) Close() error {
	// Not the responsibility of unformattedStream to close underlying stream
	// which may (or may not) be an io.WriteCloser.
	return nil
}

type formattedStream struct {
	path           string
	args           []string
	out            io.WriteCloser
	unformattedBuf *bytes.Buffer
}

func (s formattedStream) Write(p []byte) (int, error) {
	return s.unformattedBuf.Write(p)
}

func (s formattedStream) Close() error {
	defer s.out.Close()
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	cmd := exec.CommandContext(ctx, s.path, s.args...)
	formattedBuf := new(bytes.Buffer)
	cmd.Stdout = formattedBuf
	errBuf := new(bytes.Buffer)
	cmd.Stderr = errBuf
	in, err := cmd.StdinPipe()
	if err != nil {
		s.out.Write(s.unformattedBuf.Bytes())
		return err
	}
	if err := cmd.Start(); err != nil {
		s.out.Write(s.unformattedBuf.Bytes())
		return err
	}
	if _, err := in.Write(s.unformattedBuf.Bytes()); err != nil {
		s.out.Write(s.unformattedBuf.Bytes())
		return err
	}
	if err := in.Close(); err != nil {
		s.out.Write(s.unformattedBuf.Bytes())
		return err
	}
	if err := cmd.Wait(); err != nil {
		s.out.Write(s.unformattedBuf.Bytes())
		if errContent := errBuf.Bytes(); len(errContent) != 0 {
			return fmt.Errorf("%s: %s", err, string(errContent))
		}
		return err
	}
	_, err = s.out.Write(formattedBuf.Bytes())
	return err
}
