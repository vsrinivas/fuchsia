// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"errors"
	"io"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"reflect"
	"strconv"
	"sync"
	"testing"
	"time"

	"cloud.google.com/go/storage"
	"go.fuchsia.dev/fuchsia/tools/artifactory"
	"google.golang.org/api/googleapi"
)

const (
	maxFileSize = 1024
)

// A simple in-memory implementation of dataSink
type memSink struct {
	contents map[string][]byte
	mutex    sync.RWMutex
	err      error
}

func newMemSink() *memSink {
	return &memSink{
		contents: make(map[string][]byte),
	}
}

func (s *memSink) objectExistsAt(ctx context.Context, name string) (bool, *storage.ObjectAttrs, error) {
	s.mutex.RLock()
	defer s.mutex.RUnlock()
	if s.err != nil {
		return false, nil, s.err
	}
	if _, ok := s.contents[name]; !ok {
		return false, nil, nil
	}
	attrs := &storage.ObjectAttrs{CustomTime: time.Now().AddDate(0, 0, -(daysSinceCustomTime + 1))}
	return true, attrs, nil
}

func (s *memSink) write(ctx context.Context, upload *artifactory.Upload) error {
	s.mutex.Lock()
	defer s.mutex.Unlock()
	var reader io.Reader
	if upload.Source != "" {
		f, err := os.Open(upload.Source)
		if err != nil {
			return err
		}
		defer f.Close()
		reader = f
	} else {
		reader = bytes.NewBuffer(upload.Contents)
	}
	content, err := ioutil.ReadAll(reader)
	if err != nil {
		return err
	}
	s.contents[upload.Destination] = content
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
	dir := t.TempDir()
	for name, content := range contents {
		p := filepath.Join(dir, name)
		if err := os.MkdirAll(path.Dir(p), 0o700); err != nil {
			t.Fatal(err)
		}
		if err := ioutil.WriteFile(p, content, 0o400); err != nil {
			t.Fatalf("failed to write %q at %q: %v", content, p, err)
		}
	}
	return dir
}

func sinkHasExpectedContents(t *testing.T, actual, expected map[string][]byte, j int) {
	dir := newDirWithContents(t, actual)
	sink := newMemSink()
	ctx := context.Background()
	files := getUploadFiles(dir, expected)
	if err := uploadFiles(ctx, files, sink, j, ""); err != nil {
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
		sinkHasExpectedContents(t, actual, expected, 1)
	})

	t.Run("behaves under high concurrency", func(t *testing.T) {
		actual := make(map[string][]byte)
		for i := 0; i < 1000; i++ {
			s := strconv.Itoa(i)
			actual[s] = []byte(s)
		}
		expected := actual
		sinkHasExpectedContents(t, actual, expected, 100)
	})

	t.Run("only top-level files are uploaded", func(t *testing.T) {
		actual := map[string][]byte{
			"a":       []byte("one"),
			"b":       []byte("two"),
			"c/d":     []byte("three"),
			"c/e/f/g": []byte("four"),
		}
		dir := artifactory.Upload{Source: newDirWithContents(t, actual)}
		expected := []artifactory.Upload{
			{Source: filepath.Join(dir.Source, "a"), Destination: "a"},
			{Source: filepath.Join(dir.Source, "b"), Destination: "b"},
		}
		files, err := dirToFiles(context.Background(), dir)
		if err != nil {
			t.Fatalf("failed to read dir: %v", err)
		}
		if !reflect.DeepEqual(files, expected) {
			t.Fatalf("unexpected files from dir: actual: %v, expected: %v", files, expected)
		}
	})

	t.Run("all files are uploaded", func(t *testing.T) {
		actual := map[string][]byte{
			"a":       []byte("one"),
			"b":       []byte("two"),
			"c/d":     []byte("three"),
			"c/e/f/g": []byte("four"),
		}
		dir := artifactory.Upload{Source: newDirWithContents(t, actual), Recursive: true}
		expected := []artifactory.Upload{
			{Source: filepath.Join(dir.Source, "a"), Destination: "a"},
			{Source: filepath.Join(dir.Source, "b"), Destination: "b"},
			{Source: filepath.Join(dir.Source, "c/d"), Destination: "c/d"},
			{Source: filepath.Join(dir.Source, "c/e/f/g"), Destination: "c/e/f/g"},
		}
		files, err := dirToFiles(context.Background(), dir)
		if err != nil {
			t.Fatalf("failed to read dir: %v", err)
		}
		if !reflect.DeepEqual(files, expected) {
			t.Fatalf("unexpected files from dir: actual: %v, expected: %v", files, expected)
		}
	})

	t.Run("non-existent or empty sources are skipped", func(t *testing.T) {
		dir := t.TempDir()
		sink := newMemSink()
		ctx := context.Background()
		nonexistentFile := artifactory.Upload{Source: filepath.Join(dir, "nonexistent")}
		if err := uploadFiles(ctx, []artifactory.Upload{nonexistentFile}, sink, 1, ""); err != nil {
			t.Fatal(err)
		}
		if len(sink.contents) > 0 {
			t.Fatal("sink should be empty")
		}
		// Now check that uploading an empty files list also does not result in an error.
		if err := uploadFiles(ctx, []artifactory.Upload{}, sink, 1, ""); err != nil {
			t.Fatal(err)
		}
		if len(sink.contents) > 0 {
			t.Fatal("sink should be empty")
		}
	})

	t.Run("deduped objects are uploaded in objs_to_upload.txt", func(t *testing.T) {
		actual := map[string][]byte{
			"a":       []byte("one"),
			"b":       []byte("two"),
			"c/d":     []byte("three"),
			"c/e/f/g": []byte("four"),
		}
		dir := artifactory.Upload{Source: newDirWithContents(t, actual), Recursive: true, Deduplicate: true}
		files, err := dirToFiles(context.Background(), dir)
		if err != nil {
			t.Fatalf("failed to read dir: %v", err)
		}

		sink := newMemSink()
		sink.contents["b"] = []byte("two")
		sink.contents["c/d"] = []byte("three")
		expected := map[string][]byte{
			"a":                 []byte("one"),
			"b":                 []byte("two"),
			"c/d":               []byte("three"),
			"c/e/f/g":           []byte("four"),
			objsToRefreshTTLTxt: []byte("b\nc/d"),
		}

		ctx := context.Background()
		if err := uploadFiles(ctx, files, sink, 1, ""); err != nil {
			t.Fatal(err)
		}
		sinkHasContents(t, sink, expected)
	})

	t.Run("transient errors identified", func(t *testing.T) {
		// False for a regular error.
		if isTransientError(errors.New("foo")) {
			t.Fatal("non-transient error: got true, want false")
		}
		// False on HTTP response code 200.
		gErr := new(googleapi.Error)
		gErr.Code = 200
		if isTransientError(gErr) {
			t.Fatal("non-transient error: got true, want false")
		}
		// True for transient errors.
		if !isTransientError(transientError{err: errors.New("foo")}) {
			t.Fatal("explicit transient error: got false, want true")
		}
		if !isTransientError(context.DeadlineExceeded) {
			t.Fatal("explicit transient error: got false, want true")
		}
		// True on HTTP response code 500.
		gErr.Code = 500
		if !isTransientError(gErr) {
			t.Fatal("googleapi transient error: got false, want true")
		}

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

		// Confirm no error on valid upload.
		dir := newDirWithContents(t, actual)
		sink := newMemSink()
		ctx := context.Background()
		files := getUploadFiles(dir, expected)
		err := uploadFiles(ctx, files, sink, 1, "")
		if isTransientError(err) {
			t.Fatal("transient upload error: got true, want false")
		}
		// Check a non-transient error.
		sink.err = errors.New("foo")
		err = uploadFiles(ctx, files, sink, 1, "")
		if isTransientError(err) {
			t.Fatal("transient upload error: got true, want false")
		}
		// Now use a transient error.
		sink.err = transientError{err: errors.New("foo")}
		err = uploadFiles(ctx, files, sink, 1, "")
		if !isTransientError(err) {
			t.Fatal("transient upload error: got false, want true")
		}
	})
}
