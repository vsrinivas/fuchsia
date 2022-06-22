// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"bytes"
	"context"
	"io"
	"math"
	"sync"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/streams"
)

// LockedWriter is a wrapper around a writer that locks around each write so
// that multiple writes won't interleave with each other.
type LockedWriter struct {
	mu     sync.Mutex
	writer io.Writer
}

// NewLockedWriter returns a LockedWriter that associates a new lock with the
// provided writer.
func NewLockedWriter(writer io.Writer) *LockedWriter {
	return &LockedWriter{
		mu:     sync.Mutex{},
		writer: writer,
	}
}

func (w *LockedWriter) Write(data []byte) (int, error) {
	w.mu.Lock()
	defer w.mu.Unlock()
	return w.writer.Write(data)
}

// LineWriter is a wrapper around a writer that writes line by line so
// that multiple writers to the same underlying writer won't interleave
// their writes midline.
type LineWriter struct {
	writer io.Writer
	line   []byte
}

// NewLineWriter returns a new LineWriter.
func NewLineWriter(writer io.Writer) *LineWriter {
	return &LineWriter{
		writer: writer,
	}
}

// Write stores bytes until it gets a newline and then writes to the underlying
// writer line by line. If the underlying Write() returns an err, this writer
// will return the number of bytes of the current data that were written.
// Otherwise, it returns the full length of the data to notify callers that it
// has received the whole data.
func (w *LineWriter) Write(data []byte) (int, error) {
	lines := bytes.SplitAfter(data, []byte("\n"))
	written := 0
	for _, line := range lines {
		if bytes.HasSuffix(line, []byte("\n")) {
			n, err := w.writer.Write(append(w.line, line...))
			written += int(math.Max(0, float64(n-len(line))))
			if err != nil {
				return written, err
			}
			w.line = []byte{}
		} else {
			w.line = append(w.line, line...)
		}
	}
	return len(data), nil
}

// NewStiodWriters returns a new LineWriter for the stdout and stderr associated
// with the provided context. It also returns a function to flush out any
// remaining data not written by Write because it didn't end with a newline.
func NewStdioWriters(ctx context.Context) (io.Writer, io.Writer, func()) {
	stdoutWriter := NewLineWriter(streams.Stdout(ctx))
	stderrWriter := NewLineWriter(streams.Stderr(ctx))
	flush := func() {
		// Flush out the rest of the data stored by the writers.
		if len(stdoutWriter.line) > 0 {
			if _, err := stdoutWriter.Write([]byte("\n")); err != nil {
				logger.Debugf(ctx, "failed to flush out data to stdout %q: %s", string(stdoutWriter.line), err)
			}
		}
		if len(stderrWriter.line) > 0 {
			if _, err := stderrWriter.Write([]byte("\n")); err != nil {
				logger.Debugf(ctx, "failed to flush out data to stderr %q: %s", string(stderrWriter.line), err)
			}
		}
	}
	return stdoutWriter, stderrWriter, flush
}
