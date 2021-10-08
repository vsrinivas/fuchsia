// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen

import (
	"bytes"
	"context"
	"fmt"
	"os/exec"
	"time"
)

// Formatter formats generated source code
type Formatter interface {
	// Format formats source code.
	Format(source []byte) ([]byte, error)
}

// identifyFormatter returns the input unmodified
type identityFormatter struct{}

func (f identityFormatter) Format(source []byte) ([]byte, error) {
	return source, nil
}

// externalFormatter formats a writer stream.
type externalFormatter struct {
	path  string
	args  []string
	limit int
}

var _ = []Formatter{identityFormatter{}, externalFormatter{}}

const timeout = 2 * time.Minute

// NewFormatter creates a new external formatter.
//
// The `path` needs to either
// * Point to an executable which formats stdin and outputs it to stdout;
// * An empty string, in which case no formatting will occur.
func NewFormatter(path string, args ...string) Formatter {
	if path == "" {
		return identityFormatter{}
	}
	return externalFormatter{
		path: path,
		args: args,
	}
}

// NewFormatterWithSizeLimit creates a new external formatter that doesn't
// attempt to format sources over a specified size.
//
// The `path` needs to either
// * Point to an executable which formats stdin and outputs it to stdout;
// * An empty string, in which case no formatting will occur.
func NewFormatterWithSizeLimit(limit int, path string, args ...string) Formatter {
	if path == "" {
		return identityFormatter{}
	}
	return externalFormatter{
		path:  path,
		args:  args,
		limit: limit,
	}
}

func (f externalFormatter) Format(source []byte) ([]byte, error) {
	if f.limit > 0 && len(source) > f.limit {
		return source, nil
	}
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	cmd := exec.CommandContext(ctx, f.path, f.args...)
	formattedBuf := new(bytes.Buffer)
	cmd.Stdout = formattedBuf
	errBuf := new(bytes.Buffer)
	cmd.Stderr = errBuf
	in, err := cmd.StdinPipe()
	if err != nil {
		return nil, fmt.Errorf("Error creating stdin pipe: %w", err)
	}
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("Error starting formatter process: %w", err)
	}
	if _, err := in.Write(source); err != nil {
		return nil, fmt.Errorf("Error writing stdin: %w", err)
	}
	if err := in.Close(); err != nil {
		return nil, fmt.Errorf("Error closing stdin: %w", err)
	}
	if err := cmd.Wait(); err != nil {
		if errContent := errBuf.Bytes(); len(errContent) != 0 {
			return nil, fmt.Errorf("Formatter (%v) error: %w (stderr: %s)", cmd, err, string(errContent))
		}
		return nil, fmt.Errorf("Formatter (%v) error but stderr was empty: %w", cmd, err)
	}
	return formattedBuf.Bytes(), nil
}
