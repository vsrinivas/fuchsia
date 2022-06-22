// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"context"
	"io"
	"strings"
	"sync"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/lib/streams"
)

type mockWriter struct {
	w           io.Writer
	gotLock     chan struct{}
	finishWrite chan struct{}
	done        chan struct{}
}

func (m *mockWriter) Write(data []byte) (int, error) {
	m.gotLock <- struct{}{}
	// Start writing some of the data.
	n, err := m.w.Write(data[:1])
	if err != nil {
		return n, err
	}
	// Wait for the signal to finish.
	<-m.finishWrite
	n2, err := m.w.Write(data[1:])
	// Signal that the write is complete.
	m.done <- struct{}{}
	return n + n2, err
}

func TestLockedWriter(t *testing.T) {
	var w strings.Builder
	mock := &mockWriter{
		w:           &w,
		gotLock:     make(chan struct{}, 1),
		finishWrite: make(chan struct{}, 1),
		done:        make(chan struct{}, 1),
	}
	writer := NewLockedWriter(mock)
	go writer.Write([]byte("hello"))
	// Wait for the lock to be acquired.
	<-mock.gotLock
	// Attempt to start a new write.
	go writer.Write([]byte("bye"))
	// Signal for the first write to complete.
	mock.finishWrite <- struct{}{}
	// Wait for the first write to complete.
	<-mock.done
	if w.String() != "hello" {
		t.Errorf("got %q, want \"hello\"", w.String())
	}
	// Signal for the second write to complete.
	mock.finishWrite <- struct{}{}
	// Wait for the second write to complete.
	<-mock.done
	if w.String() != "hellobye" {
		t.Errorf("got %q, want \"hellobye\"", w.String())
	}
}

func write(t *testing.T, w io.Writer, data []string) {
	for _, subdata := range data {
		n, err := w.Write([]byte(subdata))
		if err != nil {
			t.Errorf("failed to write data %q: %s", subdata, err)
		}
		if n != len(subdata) {
			t.Errorf("received %d bytes for writing, want %d", n, len(subdata))
		}
	}
}

func TestStdioWriters(t *testing.T) {
	var w strings.Builder
	ctx := streams.ContextWithStdout(context.Background(), NewLockedWriter(&w))

	stdout1, _, flush1 := NewStdioWriters(ctx)
	stdout2, _, flush2 := NewStdioWriters(ctx)

	var wg sync.WaitGroup
	wg.Add(2)
	go func() {
		defer wg.Done()
		write(t, stdout1, []string{"h", "i", " ", "f", "r", "o", "m", " st", "dout1", "\n", "extra"})
	}()
	go func() {
		defer wg.Done()
		write(t, stdout2, []string{"h", "e", "l", "l", "o ", "says std", "out2\n bye", "\n", "extra2"})
	}()

	// Wait for the writers to write all their lines.
	wg.Wait()
	// Flush the rest of the data not ending in a newline.
	flush1()
	flush2()

	for _, expectedLines := range [][]string{
		{"hi from stdout1", "extra"},
		{"hello says stdout2", "bye", "extra2"},
	} {
		startIndex := 0
		for _, line := range expectedLines {
			i := strings.Index(w.String(), line)
			if i < startIndex {
				t.Errorf("line %q missing or out of order from: %q", line, w.String())
			} else {
				startIndex = i + len(line)
			}
		}
	}
}
