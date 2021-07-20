// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package iomisc

import (
	"bytes"
	"context"
	"errors"
	"io"
	"testing"
	"time"
)

func TestMatchingReader(t *testing.T) {
	t.Run("sequence appears in a single read", func(t *testing.T) {
		sequence := []byte("ABCDE")
		var buf bytes.Buffer
		m := NewMatchingReader(&buf, [][]byte{sequence})
		assertMatch(t, m, nil)

		buf.Write(sequence)
		p := make([]byte, 1024)
		if _, err := m.Read(p); err != nil && !errors.Is(err, io.EOF) {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, sequence)
	})

	t.Run("sequence appears across multiple reads", func(t *testing.T) {
		sequence := []byte("ABCDE")
		var buf bytes.Buffer
		m := NewMatchingReader(&buf, [][]byte{sequence})
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
		if _, err := m.Read(p); err != nil && !errors.Is(err, io.EOF) {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, sequence)
	})

	t.Run("Read throws EOFs after match", func(t *testing.T) {
		sequence := []byte("ABCDE")
		var buf bytes.Buffer
		m := NewMatchingReader(&buf, [][]byte{sequence})
		assertMatch(t, m, nil)

		buf.Write([]byte("ABCDE"))
		p := make([]byte, 1024)
		if _, err := m.Read(p); err != nil && !errors.Is(err, io.EOF) {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, sequence)

		buf.Write([]byte("FGHIJK"))
		p = make([]byte, 1024)
		if _, err := m.Read(p); !errors.Is(err, io.EOF) {
			t.Fatalf("unexpected error: expected EOF; got %v", err)
		}
		assertMatch(t, m, sequence)
	})

	t.Run("multiple sequences", func(t *testing.T) {
		sequences := [][]byte{[]byte("ABCDE"), []byte("BCDEF")}
		var buf bytes.Buffer
		m := NewMatchingReader(&buf, sequences)
		assertMatch(t, m, nil)

		buf.Write([]byte("BCDE"))
		p := make([]byte, 1024)
		if _, err := m.Read(p); err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, nil)

		buf.Write([]byte("FGHIJK"))
		p = make([]byte, 1024)
		if _, err := m.Read(p); err != nil && !errors.Is(err, io.EOF) {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, sequences[1])
	})

	t.Run("sequence appears mid-read", func(t *testing.T) {
		sequence := []byte("BC")
		var buf bytes.Buffer
		m := NewMatchingReader(&buf, [][]byte{sequence})
		assertMatch(t, m, nil)
		buf.Write([]byte("ABCD"))
		p := make([]byte, 1024)
		if _, err := m.Read(p); err != nil && !errors.Is(err, io.EOF) {
			t.Fatalf("unexpected error: %v", err)
		}
		assertMatch(t, m, sequence)
	})
}

func assertMatch(t *testing.T, m *MatchingReader, match []byte) {
	t.Helper()
	if bytes.Compare(match, m.Match()) != 0 {
		t.Fatalf("expected match of %q; not %q", match, m.Match())
	}
}

func TestReadUntilMatch(t *testing.T) {
	t.Run("success", func(t *testing.T) {
		r, w := io.Pipe()
		defer w.Close()
		defer r.Close()

		m := NewMatchingReader(r, [][]byte{[]byte("ABCDE")})

		go func() {
			w.Write([]byte("ABC"))
			w.Write([]byte("D"))
			w.Write([]byte("EFGH"))
		}()

		match, err := ReadUntilMatch(context.Background(), m)
		if err != nil {
			t.Fatalf("unexpected error: %v", err)
		}
		if bytes.Compare(match, []byte("ABCDE")) != 0 {
			t.Fatalf("expected match of ABCDE; not %q", match)
		}
	})

	t.Run("read fails", func(t *testing.T) {
		r, w := io.Pipe()
		defer r.Close()

		m := NewMatchingReader(r, [][]byte{[]byte("foo")})

		go func() {
			w.Write([]byte("bar"))
			w.Close()
		}()

		_, err := ReadUntilMatch(context.Background(), m)

		if !errors.Is(err, io.EOF) {
			t.Errorf("ReadUntilMatch() returned %v, want io.EOF", err)
		}
	})

	t.Run("cancellation", func(t *testing.T) {
		r, w := io.Pipe()
		defer r.Close()
		defer w.Close()

		m := NewMatchingReader(r, [][]byte{[]byte("A")})

		ctx, cancel := context.WithDeadline(context.Background(), time.Now().Add(10*time.Millisecond))
		defer cancel()

		go func() {
			b := []byte("B")
			for {
				w.Write(b)
			}
		}()

		if _, err := ReadUntilMatch(ctx, m); err == nil || !errors.Is(err, context.DeadlineExceeded) {
			t.Errorf("ReadUntilMatch() returned %v, want DeadlineExceeded ", err)
		}
	})
}
