// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"crypto/md5"
	"errors"
	"io"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"strconv"
	"sync"
	"testing"
)

const (
	maxFileSize = 1024
)

// A simple in-memory implementation of dataSink
type memSink struct {
	contents map[string][]byte
	mutex    sync.RWMutex
}

func newMemSink() *memSink {
	return &memSink{
		contents: make(map[string][]byte),
	}
}

func (s memSink) getNamespace() string {
	// Unimportant. Only explicitly used in uploadFilesAt() for more verbose logging.
	return "namespace"
}

func (s *memSink) objectExistsAt(ctx context.Context, name string, expectedChecksum []byte) (bool, error) {
	s.mutex.RLock()
	defer s.mutex.RUnlock()
	content, ok := s.contents[name]
	if !ok {
		return false, nil
	}
	actualChecksum := md5.Sum(content)
	if bytes.Compare(expectedChecksum, actualChecksum[:]) != 0 {
		return true, checksumError{
			name:     name,
			expected: expectedChecksum,
			actual:   actualChecksum[:],
		}
	}
	return true, nil
}

func (s *memSink) write(ctx context.Context, name string, path string, expectedChecksum []byte) error {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	content, err := ioutil.ReadFile(path)
	if err != nil {
		return err
	}
	actualChecksum := md5.Sum(content)
	if bytes.Compare(expectedChecksum, actualChecksum[:]) != 0 {
		return checksumError{
			name:     name,
			expected: expectedChecksum,
			actual:   actualChecksum[:],
		}
	}
	s.contents[name] = content
	return nil
}

func sinkHasContents(t *testing.T, s *memSink, contents map[string][]byte) {
	if len(s.contents) > len(contents) {
		t.Fatalf("the sink has more contents than expected")
	} else if len(s.contents) < len(contents) {
		t.Fatal("the sink has less contents than expected")
	}

	for name, actual := range s.contents {
		expected, ok := contents[name]
		if !ok {
			t.Fatalf("found unexpected content %q with name %q in sink", actual, name)
		} else if string(expected) != string(actual) {
			t.Fatalf("unexpected content with name %q: %q != %q", name, actual, expected)
		}
	}
}

// Given a mapping of relative filepath to byte contents, this utility populates
// a temporary directory with those contents at those paths.
func newDirWithContents(t *testing.T, contents map[string][]byte) string {
	dir, err := ioutil.TempDir("", "artifactory")
	if err != nil {
		t.Fatal(err)
	}
	for name, content := range contents {
		p := filepath.Join(dir, name)
		if err := os.MkdirAll(path.Dir(p), os.ModePerm); err != nil {
			t.Fatal(err)
		}
		// 0444 == read-only.
		if err := ioutil.WriteFile(p, content, 0444); err != nil {
			t.Fatalf("failed to write %q at %q: %v", content, p, err)
		}
	}
	return dir
}

func sinkHasExpectedContents(t *testing.T, actual, expected map[string][]byte, opts uploadOptions) {
	dir := newDirWithContents(t, actual)
	defer os.RemoveAll(dir)
	sink := newMemSink()
	ctx := context.Background()
	if err := uploadFilesAt(ctx, dir, sink, opts); err != nil {
		t.Fatalf("failed to upload contents: %v", err)
	}
	sinkHasContents(t, sink, expected)
}

func TestUploading(t *testing.T) {
	t.Run("simple case", func(t *testing.T) {
		actual := map[string][]byte{
			"a": []byte("one"),
			"b": []byte("two"),
		}
		expected := actual
		opts := uploadOptions{j: 1}
		sinkHasExpectedContents(t, actual, expected, opts)
	})

	t.Run("behaves under high concurrency", func(t *testing.T) {
		actual := make(map[string][]byte)
		for i := 0; i < 1000; i++ {
			s := strconv.Itoa(i)
			actual[s] = []byte(s)
		}
		expected := actual
		opts := uploadOptions{j: 100}
		sinkHasExpectedContents(t, actual, expected, opts)
	})

	t.Run("only top-level files are uploaded", func(t *testing.T) {
		actual := map[string][]byte{
			"a":       []byte("one"),
			"b":       []byte("two"),
			"c/d":     []byte("three"),
			"c/e/f/g": []byte("four"),
		}
		expected := map[string][]byte{
			"a": []byte("one"),
			"b": []byte("two"),
		}
		opts := uploadOptions{j: 1}
		sinkHasExpectedContents(t, actual, expected, opts)
	})

	t.Run("collision behavior is parametrized", func(t *testing.T) {
		var err error
		ctx := context.Background()
		srcContents := map[string][]byte{
			"a": []byte("one"),
			"b": []byte("two"),
		}
		dir := newDirWithContents(t, srcContents)
		defer os.RemoveAll(dir)

		sink := newMemSink()
		sink.contents = srcContents
		expectedFailureOpts := uploadOptions{j: 1, failOnCollision: true}
		if err = uploadFilesAt(ctx, dir, sink, expectedFailureOpts); err == nil {
			t.Fatal("upload succeeded when it should have failed")
		}

		sink = newMemSink()
		sink.contents = srcContents
		expectedSuccessOpts := uploadOptions{j: 1, failOnCollision: false}
		if err = uploadFilesAt(ctx, dir, sink, expectedSuccessOpts); err != nil {
			t.Fatal(err)
		}

		sink = newMemSink()
		sink.contents["a"] = []byte("checksum mismatch!")
		expectedChecksum := md5.Sum(srcContents["a"])
		actualChecksum := md5.Sum(sink.contents["a"])
		genericOpts := uploadOptions{j: 1}
		err = uploadFilesAt(ctx, dir, sink, genericOpts)

		actualChecksumErr, ok := err.(checksumError)
		if !ok {
			t.Fatal("expected a checksum error")
		}
		expectedChecksumErr := checksumError{
			name:     "a",
			expected: expectedChecksum[:],
			actual:   actualChecksum[:],
		}
		if actualChecksumErr.Error() != expectedChecksumErr.Error() {
			t.Fatalf("differing checksum errors found:\nexpected: %q;\nactual: %q\n", expectedChecksumErr, actualChecksumErr)
		}
	})

	t.Run("non-existent or empty sources are skipped", func(t *testing.T) {
		dir, err := ioutil.TempDir("", "artifactory")
		if err != nil {
			t.Fatal(err)
		}
		defer os.RemoveAll(dir)
		sink := newMemSink()
		ctx := context.Background()
		opts := uploadOptions{j: 1}
		nonexistentDir := filepath.Join(dir, "nonexistent")
		if err = uploadFilesAt(ctx, nonexistentDir, sink, opts); err != nil {
			t.Fatal(err)
		}
		if len(sink.contents) > 0 {
			t.Fatal("sink should be empty")
		}
		// Now check that uploading from the empty `dir` too does not result in an error.
		if err = uploadFilesAt(ctx, dir, sink, opts); err != nil {
			t.Fatal(err)
		}
		if len(sink.contents) > 0 {
			t.Fatal("sink should be empty")
		}
	})
}

// Implements io.WriteCloser and returns a custom error on close.
type writer struct {
	err error
}

func (writer) Write(data []byte) (int, error) {
	return len(data), nil
}

func (w writer) Close() error {
	return w.err
}

// Implements io.Reader
type reader struct {
	err error
}

func (r reader) Read(data []byte) (int, error) {
	return 0, r.err
}

func TestCopyToCloud(t *testing.T) {
	alreadyExistsErr := errors.New(err412Msg)
	failureErr := errors.New("failed")

	handleCase := func(t *testing.T, copyErr, closeErr, expected error) {
		ctx := context.Background()
		r := reader{err: copyErr} // a Read error will manifest as an error for io.CopyN
		w := writer{err: closeErr}
		actual := copyToCloud(ctx, "name", r, w)

		if expected != actual {
			t.Fatalf("expected %v; got %v", expected, actual)
		}
	}

	cases := []struct {
		name     string
		copyErr  error
		closeErr error
		expected error
	}{
		{
			"no errors encountered",
			io.EOF,
			nil,
			nil,
		},
		{
			"failure on copy",
			failureErr,
			nil,
			failureErr,
		},
		{
			"failure on close",
			io.EOF,
			failureErr,
			failureErr,
		},
		{
			"412 on copy",
			alreadyExistsErr,
			nil,
			nil,
		},
		{
			"412 on close",
			io.EOF,
			alreadyExistsErr,
			nil,
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			handleCase(t, c.copyErr, c.closeErr, c.expected)
		})
	}
}
