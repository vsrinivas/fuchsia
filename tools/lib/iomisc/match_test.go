// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package iomisc

import (
	"bytes"
	"context"
	"io"
	"regexp"
	"testing"
)

func TestMatchingReader(t *testing.T) {
	t.Run("sequence appears in a single read", func(t *testing.T) {
		sequence := "ABCDE"
		var buf bytes.Buffer
		m := NewSequenceMatchingReader(&buf, sequence)
		assertMatch(t, m, nil)

		buf.Write([]byte(sequence))
		p := make([]byte, 1024)
		if _, err := m.Read(p); err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, []byte(sequence))
	})

	t.Run("sequence appears across multiple reads", func(t *testing.T) {
		sequence := "ABCDE"
		var buf bytes.Buffer
		m := NewSequenceMatchingReader(&buf, sequence)
		assertMatch(t, m, nil)

		buf.Write([]byte("ABC"))
		p := make([]byte, 1024)
		if _, err := m.Read(p); err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, nil)

		buf.Write([]byte("D"))
		p = make([]byte, 1024)
		if _, err := m.Read(p); err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, nil)

		buf.Write([]byte("EFGH"))
		p = make([]byte, 1024)
		if _, err := m.Read(p); err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, []byte(sequence))
	})

	t.Run("pattern appears in a single read", func(t *testing.T) {
		pattern := "a[0-9]{1,2}[B-D]"
		re := regexp.MustCompile(pattern)
		var buf bytes.Buffer
		m := NewPatternMatchingReader(&buf, re, 4)
		assertMatch(t, m, nil)

		buf.Write([]byte("a03C"))
		p := make([]byte, 1024)
		if _, err := m.Read(p); err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, []byte("a03C"))
	})

	t.Run("pattern appears across multiple reads", func(t *testing.T) {
		pattern := "a[0-9]{1,2}[B-D]"
		re := regexp.MustCompile(pattern)
		var buf bytes.Buffer
		m := NewPatternMatchingReader(&buf, re, 4)
		assertMatch(t, m, nil)

		buf.Write([]byte("12345a"))
		p := make([]byte, 1024)
		if _, err := m.Read(p); err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, []byte(nil))

		buf.Write([]byte("03"))
		p = make([]byte, 1024)
		if _, err := m.Read(p); err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, []byte(nil))

		buf.Write([]byte("C"))
		p = make([]byte, 1024)
		if _, err := m.Read(p); err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, []byte("a03C"))
	})

	t.Run("Read throws EOFs after match", func(t *testing.T) {
		pattern := "ABCDE"
		re := regexp.MustCompile(pattern)
		var buf bytes.Buffer
		m := NewPatternMatchingReader(&buf, re, len(pattern))
		assertMatch(t, m, nil)

		buf.Write([]byte("ABCDE"))
		p := make([]byte, 1024)
		if _, err := m.Read(p); err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, []byte(pattern))

		buf.Write([]byte("FGHIJK"))
		p = make([]byte, 1024)
		if _, err := m.Read(p); err != io.EOF {
			t.Fatalf("unexpected error: expected EOF; got %v", err)
		}
		assertMatch(t, m, []byte(pattern))
	})
}

func assertMatch(t *testing.T, m *MatchingReader, match []byte) {
	t.Helper()
	if bytes.Compare(match, m.Match()) != 0 {
		t.Fatalf("expected match of %q; not %q", match, m.Match())
	}
}

func TestReadUntilMatch(t *testing.T) {
	r, w := io.Pipe()
	defer w.Close()
	defer r.Close()

	m := NewPatternMatchingReader(r, regexp.MustCompile("ABCD(E|F)"), 5)

	go func() {
		w.Write([]byte("ABC"))
		w.Write([]byte("D"))
		w.Write([]byte("EFGH"))
	}()
	match, err := ReadUntilMatch(context.Background(), m, nil)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if bytes.Compare(match, []byte("ABCDE")) != 0 {
		t.Fatalf("expected match of ABCDE; not %q", match)
	}
}
