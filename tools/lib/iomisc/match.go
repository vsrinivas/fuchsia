// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package iomisc

import (
	"bytes"
	"context"
	"errors"
	"io"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

// MatchingReader is an io.Reader implementation that wraps another such
// implementation. It reads only up until one of the sequences has been read consecutively.
type MatchingReader struct {
	r        io.Reader
	toMatch  [][]byte
	progress []int
	matchIdx int
}

// NewMatchingReader returns a MatchingReader that matches any of toMatch.
func NewMatchingReader(reader io.Reader, toMatch ...[]byte) *MatchingReader {
	return &MatchingReader{
		r:        reader,
		toMatch:  toMatch,
		progress: make([]int, len(toMatch)),
		matchIdx: -1,
	}
}

// Match returns the first match among the bytes read, or nil if there
// has yet to be a match.
func (m *MatchingReader) Match() []byte {
	if m.matchIdx >= 0 {
		return m.toMatch[m.matchIdx]
	}
	return nil
}

// Read reads from the underlying reader and checks whether the pattern
// has been matched among the bytes read. Once a match has been found,
// subsequent reads will return an io.EOF.
func (m *MatchingReader) Read(p []byte) (int, error) {
	if m.matchIdx >= 0 {
		return 0, io.EOF
	}
	n, err := m.r.Read(p)
	p = p[:n]
	for i, tm := range m.toMatch {
		for j := 0; j < n; j++ {
			remainingToMatch := tm[m.progress[i]:]
			relevantP := p[j:min(len(remainingToMatch)+j, len(p))]
			if bytes.HasPrefix(remainingToMatch, relevantP) {
				m.progress[i] += len(relevantP)
				if m.progress[i] == len(tm) {
					m.matchIdx = i
				}
				break
			} else {
				m.progress[i] = 0
			}
		}
		if m.matchIdx >= 0 {
			return n, io.EOF
		}
	}
	return n, err
}

// ReadUntilMatch reads from a Reader until it encounters an occurrence of one
// of the byte slices specified in toMatch.
// Checks ctx for cancellation only between calls to m.Read(), so cancellation
// will not be noticed if m.Read() blocks.
// See https://github.com/golang/go/issues/20280 for discussion of similar issues.
func ReadUntilMatch(ctx context.Context, reader io.Reader, toMatch ...[]byte) ([]byte, error) {
	m := NewMatchingReader(reader, toMatch...)
	// buf size considerations: smaller => more responsive to ctx cancellation,
	// larger => less CPU overhead.
	buf := make([]byte, 1024)
	lastReadSize := 0
	for ctx.Err() == nil {
		var err error
		lastReadSize, err = m.Read(buf)
		if err == nil {
			continue
		}
		if errors.Is(err, io.EOF) {
			if match := m.Match(); match != nil {
				return match, nil
			}
		}
		return nil, err
	}

	// If we time out, it is helpful to see the last bytes processed.
	logger.Debugf(ctx, "ReadUntilMatch: last %d bytes read before cancellation: %q", lastReadSize, buf[:lastReadSize])

	return nil, ctx.Err()
}

// ReadUntilMatchString has identical behavior to ReadUntilMatch, but accepts
// and returns strings instead of byte slices.
func ReadUntilMatchString(ctx context.Context, reader io.Reader, strings ...string) (string, error) {
	var toMatch [][]byte
	for _, s := range strings {
		toMatch = append(toMatch, []byte(s))
	}
	b, err := ReadUntilMatch(ctx, reader, toMatch...)
	return string(b), err
}

func min(a, b int) int {
	if a <= b {
		return a
	}
	return b
}
