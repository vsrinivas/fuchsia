// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package main

import (
	"bytes"
	"compress/gzip"
	"context"
	"crypto/md5"
	"io"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"reflect"
	"strconv"
	"sync"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/artifactory/lib"
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

func (s *memSink) objectExistsAt(ctx context.Context, name string) (bool, error) {
	s.mutex.RLock()
	defer s.mutex.RUnlock()
	if _, ok := s.contents[name]; !ok {
		return false, nil
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
	actualChecksum := md5Sum(content)
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

func md5Sum(b []byte) []byte {
	h := md5.New()
	gzw := gzip.NewWriter(h)
	r := bytes.NewReader(b)
	if _, err := io.Copy(gzw, r); err != nil {
		gzw.Close()
		panic("failed to calcular md5 hash: " + err.Error())
	}
	if err := gzw.Close(); err != nil {
		panic("failed to close gzip writer: " + err.Error())
	}
	checksum := h.Sum(nil)
	return checksum[:]
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
	files := getUploadFiles(dir, expected)
	if err := uploadFiles(ctx, files, sink, opts); err != nil {
		t.Fatalf("failed to upload contents: %v", err)
	}
	sinkHasContents(t, sink, expected)
}

func getUploadFiles(dir string, fileContents map[string][]byte) []artifactory.Upload {
	var files []artifactory.Upload
	for f := range fileContents {
		files = append(files, artifactory.Upload{
			Source:      filepath.Join(dir, f),
			Destination: f,
		})
	}
	return files
}

func TestUploading(t *testing.T) {
	t.Run("uploads specific files", func(t *testing.T) {
		actual := map[string][]byte{
			"a":       []byte("one"),
			"b":       []byte("two"),
			"c/d":     []byte("three"),
			"c/e/f/g": []byte("four"),
		}
		expected := map[string][]byte{
			"a":   []byte("one"),
			"c/d": []byte("three"),
		}
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
		dir := artifactory.Upload{Source: newDirWithContents(t, actual)}
		defer os.RemoveAll(dir.Source)
		expected := []artifactory.Upload{
			{Source: filepath.Join(dir.Source, "a"), Destination: "a"},
			{Source: filepath.Join(dir.Source, "b"), Destination: "b"},
		}
		files, err := dirToFiles(dir)
		if err != nil {
			t.Fatalf("failed to read dir: %v", err)
		}
		if !reflect.DeepEqual(files, expected) {
			t.Fatalf("unexpected files from dir: actual: %v, expected: %v", files, expected)
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
		nonexistentFile := artifactory.Upload{Source: filepath.Join(dir, "nonexistent")}
		if err = uploadFiles(ctx, []artifactory.Upload{nonexistentFile}, sink, opts); err != nil {
			t.Fatal(err)
		}
		if len(sink.contents) > 0 {
			t.Fatal("sink should be empty")
		}
		// Now check that uploading an empty files list also does not result in an error.
		if err = uploadFiles(ctx, []artifactory.Upload{}, sink, opts); err != nil {
			t.Fatal(err)
		}
		if len(sink.contents) > 0 {
			t.Fatal("sink should be empty")
		}
	})
}
