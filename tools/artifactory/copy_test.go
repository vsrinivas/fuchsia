// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package artifactory

import (
	"context"
	"errors"
	"io"
	"testing"
)

// Implements io.WriteCloser and returns a custom error on close.
type writer struct {
	err error
}

func (writer) Write(data []byte) (int, error) {
	return len(data), nil
}

func (w writer) Close() error {
	return w.err
}

// Implements io.Reader
type reader struct {
	err error
}

func (r reader) Read(data []byte) (int, error) {
	return 0, r.err
}

func TestCopy(t *testing.T) {
	alreadyExistsErr := errors.New(err412Msg)
	failureErr := errors.New("failed")

	handleCase := func(t *testing.T, copyErr, closeErr, expected error) {
		ctx := context.Background()
		r := reader{err: copyErr} // a Read error will manifest as an error for io.CopyN
		w := writer{err: closeErr}
		actual := Copy(ctx, "name", r, w /* chunkSize = */, 10)

		if expected != actual {
			t.Fatalf("expected %v; got %v", expected, actual)
		}
	}

	cases := []struct {
		name     string
		copyErr  error
		closeErr error
		expected error
	}{
		{
			"no errors encountered",
			io.EOF,
			nil,
			nil,
		},
		{
			"failure on copy",
			failureErr,
			nil,
			failureErr,
		},
		{
			"failure on close",
			io.EOF,
			failureErr,
			failureErr,
		},
		{
			"412 on copy",
			alreadyExistsErr,
			nil,
			nil,
		},
		{
			"412 on close",
			io.EOF,
			alreadyExistsErr,
			nil,
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			handleCase(t, c.copyErr, c.closeErr, c.expected)
		})
	}
}
