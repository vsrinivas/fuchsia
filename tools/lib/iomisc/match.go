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
	"go.fuchsia.dev/fuchsia/tools/lib/ring"
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
func NewMatchingReader(reader io.Reader, toMatch [][]byte) *MatchingReader {
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
			break
		}
	}
	return n, err
}

type byteTracker interface {
	Bytes() []byte
}

// ReadUntilMatch reads from a MatchingReader until a match has been read,
// and ultimately tries to return those matches.
func ReadUntilMatch(ctx context.Context, m *MatchingReader, output io.Writer) ([]byte, error) {
	matchWindowSize := 0
	for _, tm := range m.toMatch {
		if len(tm) > matchWindowSize {
			matchWindowSize = len(tm)
		}
	}

	if output == nil {
		output = ring.NewBuffer(matchWindowSize)
	}

	errs := make(chan error)
	go func() {
		if _, err := io.Copy(output, m); err != nil {
			errs <- err
		}
		errs <- io.EOF
	}()

	select {
	case <-ctx.Done():
		// If we time out, it is helpful to see what the last bytes processed
		// were: dump these bytes if we can.
		if bt, ok := output.(byteTracker); ok {
			lastBytes := bt.Bytes()
			numBytes := min(len(lastBytes), matchWindowSize)
			lastBytes = lastBytes[len(lastBytes)-numBytes:]
			logger.Debugf(ctx, "ReadUntilMatch: last %d bytes read before cancellation: %q", numBytes, lastBytes)
		}
		// TODO(garymm): We should really interrupt the io.Copy() goroutine here.
		return nil, ctx.Err()
	case err := <-errs:
		if errors.Is(err, io.EOF) {
			if match := m.Match(); match != nil {
				return match, nil
			}
		}
		return nil, err
	}
}

func min(a, b int) int {
	if a <= b {
		return a
	}
	return b
}
