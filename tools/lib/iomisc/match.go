// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package iomisc

import (
	"bytes"
	"fmt"
	"io"
	"regexp"
	"sync"

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

// NewPatterMatchingReader returns a matching reader wrapping a given reader and
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

func min(a, b int) int {
	if a <= b {
		return a
	}
	return b
}
