// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package iomisc

import (
	"io"
	"strings"
	"testing"
)

func TestReaderAtToReader(t *testing.T) {
	r := ReaderAtToReader(strings.NewReader("123456789"))
	if _, ok := r.(io.ReaderAt); ok {
		t.Fatalf("reader implements io.ReaderAt when it shouldn't")
	}
	tests := []struct {
		byteLen        int
		expectedErr    error
		expectedOutput string
	}{
		{byteLen: 4, expectedErr: nil, expectedOutput: "1234"},
		{byteLen: 4, expectedErr: nil, expectedOutput: "5678"},
		{byteLen: 4, expectedErr: io.EOF, expectedOutput: "9"},
		{byteLen: 4, expectedErr: io.EOF, expectedOutput: ""},
	}
	for _, test := range tests {
		buf := make([]byte, test.byteLen)
		n, err := r.Read(buf)
		if err != test.expectedErr {
			t.Errorf("got err: %v, expected: %v", err, test.expectedErr)
		}
		if n != len(test.expectedOutput) {
			t.Errorf("read %d bytes, expected: %d", n, len(test.expectedOutput))
		}
		if string(buf[:n]) != test.expectedOutput {
			t.Errorf("read: %s, expected: %s", string(buf), test.expectedOutput)
		}
	}
}
