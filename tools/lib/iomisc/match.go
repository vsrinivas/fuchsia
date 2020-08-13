// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package iomisc

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"regexp"
	"sync"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/ring"
)

// MatchingReader is an io.Reader implementation that wraps another such
// implementation. It reads only up until an associated pattern
// has been matched among the bytes cumulatively read.
type MatchingReader struct {
	sync.Mutex
	r io.Reader
	// findMatch returns the first match of the associated pattern
	// within the provided byte slice with a given maximum size.
	findMatch    func([]byte) []byte
	window       *ring.Buffer
	maxMatchSize int
	match        []byte
}

// NewPatternMatchingReader returns a matching reader wrapping a given reader and
// pattern looking for a match up to the given size.
func NewPatternMatchingReader(reader io.Reader, pattern *regexp.Regexp, maxMatchSize int) *MatchingReader {
	if maxMatchSize <= 0 {
		panic(fmt.Sprintf("maxMatchSize (%d) must be positive", maxMatchSize))
	}
	return &MatchingReader{
		r: reader,
		findMatch: func(b []byte) []byte {
			matches := pattern.FindAll(b, -1)
			for _, m := range matches {
				if len(m) <= maxMatchSize {
					return m
				}
			}
			return nil
		},
		window:       ring.NewBuffer(2 * maxMatchSize),
		maxMatchSize: maxMatchSize,
	}
}

// NewSequenceMatchingReader returns a MatchingReader that matches a literal sequence.
func NewSequenceMatchingReader(reader io.Reader, sequence string) *MatchingReader {
	return &MatchingReader{
		r: reader,
		findMatch: func(b []byte) []byte {
			if bytes.Contains(b, []byte(sequence)) {
				return []byte(sequence)
			}
			return nil
		},
		window:       ring.NewBuffer(2 * len(sequence)),
		maxMatchSize: len(sequence),
	}
}

// Match returns the first match among the bytes read, returning nil if there
// has yet to be a match.
func (m *MatchingReader) Match() []byte {
	m.Lock()
	defer m.Unlock()
	return m.match
}

// Read reads from the underlying reader and checks whether the pattern
// has been matched among the bytes read. Once a match has been found,
// subsequent reads will return an io.EOF.
func (m *MatchingReader) Read(p []byte) (int, error) {
	if m.match != nil {
		return 0, io.EOF
	}
	n, err := m.r.Read(p)
	p = p[:n]
	for len(p) > 0 {
		maxBytes := min(m.maxMatchSize, len(p))
		m.window.Write(p[:maxBytes])
		if match := m.findMatch(m.window.Bytes()); match != nil {
			m.Lock()
			m.match = match
			m.Unlock()
			break
		}
		p = p[maxBytes:]
	}
	return n, err
}

// MaxMatchSize returns the configured maximum size a match could have.
func (m *MatchingReader) MaxMatchSize() int {
	return m.maxMatchSize
}

type byteTracker interface {
	Bytes() []byte
}

// ReadUntilMatch reads from a MatchingReader until a match has been read,
// and ultimately tries to return those matches.
func ReadUntilMatch(ctx context.Context, m *MatchingReader, output io.Writer) ([]byte, error) {
	matchWindowSize := 2 * m.MaxMatchSize()

	if output == nil {
		output = ring.NewBuffer(matchWindowSize)
	}

	errs := make(chan error)
	go func() {
		for {
			if _, err := io.Copy(output, m); err != nil {
				errs <- err
				break
			}
		}
	}()

	mc := make(chan struct{})
	go func() {
		for m.Match() == nil {
		}
		mc <- struct{}{}
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
		return nil, ctx.Err()
	case err := <-errs:
		return nil, err
	case <-mc:
	}

	return m.Match(), nil
}

func min(a, b int) int {
	if a <= b {
		return a
	}
	return b
}
