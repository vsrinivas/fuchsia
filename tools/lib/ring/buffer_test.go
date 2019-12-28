// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package ring

import (
	"bytes"
	"io"
	"io/ioutil"
	"testing"
)

func TestBuffer(t *testing.T) {
	// A sanity check.
	t.Run("implements io.ReadWriter", func(t *testing.T) {
		var _ io.ReadWriter = (*Buffer)(nil)
	})

	roundtrips := []struct {
		name     string
		size     int
		in       []byte
		expected []byte
	}{
		{
			name:     "length < size (1)",
			size:     7,
			in:       []byte("1"),
			expected: []byte("1"),
		},
		{
			name:     "length < size (2)",
			size:     7,
			in:       []byte("123456"),
			expected: []byte("123456"),
		},
		{
			name:     "length = size",
			size:     7,
			in:       []byte("1234567"),
			expected: []byte("1234567"),
		},
		{
			name:     "length > size (1)",
			size:     7,
			in:       []byte("abcdefghi"),
			expected: []byte("cdefghi"),
		},
		{
			name:     "length > size (2)",
			size:     7,
			in:       []byte("abcdefghij"),
			expected: []byte("defghij"),
		},
		{
			name:     "length > size (3)",
			size:     7,
			in:       []byte("abcdefghijk"),
			expected: []byte("efghijk"),
		},
		{
			name:     "length >> size",
			size:     5,
			in:       []byte("abcdefghijklmnopqrstuvwxyz"),
			expected: []byte("vwxyz"),
		},
		{
			name:     "size 1 buffer",
			size:     1,
			in:       []byte("12345"),
			expected: []byte("5"),
		},
		{
			name:     "size 2 buffer",
			size:     2,
			in:       []byte("12345"),
			expected: []byte("45"),
		},
	}

	for _, rt := range roundtrips {
		t.Run(rt.name, func(t *testing.T) {
			rb := NewBuffer(rt.size)
			n, err := rb.Write(rt.in)
			if err != nil {
				t.Fatalf("unexpected write error: %v", err)
			} else if n != len(rt.in) {
				t.Fatalf("wrote an incorrect number of bytes:\nexpected: %d\nactual: %d", len(rt.in), n)
			}

			actual, err := ioutil.ReadAll(rb)
			if err != nil && err != io.EOF {
				t.Fatalf("failed to read everything from the wring buffer: %v", err)
			}
			if bytes.Compare(rt.expected, actual) != 0 {
				t.Errorf("bytes read were not as expected:\nexpected: %q\nactual: %q", rt.expected, actual)
			}
		})
	}
}
