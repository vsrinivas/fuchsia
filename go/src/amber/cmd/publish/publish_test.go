// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"bytes"
	"fmt"
	"io/ioutil"
	"math/rand"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	tuf "github.com/flynn/go-tuf"
)

func TestCopyFile(t *testing.T) {
	fileSize := 8193
	src, fileContent := writeRandFile(t, fileSize)
	defer os.Remove(src)

	dst := filepath.Join(os.TempDir(), fmt.Sprintf("publish-test-dest-%x", fileContent[fileSize-8:]))
	defer os.Remove(dst)

	if err := copyFile(dst, src); err != nil {
		t.Fatalf("File copy errored out! %v", err)
	}
	fInfo, err := os.Stat(dst)
	if err != nil || fInfo.Size() != int64(fileSize) {
		t.Fatalf("Copied file does not exist or is wrong size")
	}

	copyContent, err := ioutil.ReadFile(dst)

	if len(copyContent) != fileSize || err != nil {
		t.Fatalf("Copied file could not be read")
	}

	if bytes.Compare(copyContent, fileContent) != 0 {
		t.Fatalf("Copied file content does not match")
	}
}

func TestCopyMakingDirs(t *testing.T) {
	fileSize := 8193
	src, _ := writeRandFile(t, fileSize)
	defer os.Remove(src)

	nonPath := filepath.Join(os.TempDir(), "abc", "789")
	defer os.Remove(filepath.Join(os.TempDir(), "abc"))
	defer os.Remove(filepath.Join(os.TempDir(), "abc", "789"))

	err := copyFile(nonPath, src)
	if err != nil {
		t.Fatalf("Non-existent path rejected when requesting directory creation")
	}
}

func TestCopyInvalidSrcDst(t *testing.T) {
	dst, err := ioutil.TempFile("", "publish-test-file")

	if err != nil {
		t.Fatalf("Could not create temporary destiation file")
	}
	dst.Close()
	defer os.Remove(dst.Name())

	// try making the source file a directory
	srcDir, err := ioutil.TempDir("", "publish-test-srcdir")
	if err != nil {
		t.Fatalf("Could not create temporary directory")
	}
	defer os.Remove(srcDir)

	err = copyFile(dst.Name(), srcDir)
	if err == nil {
		t.Fatalf("No error returned from copy")
	}
	if err != os.ErrInvalid {
		t.Fatalf("Copy rejected directory as source, but returned unexpected error %v", err)
	}

	err = copyFile(srcDir, dst.Name())
	if err == nil {
		t.Fatalf("Copy accepted directory as destination")
	}
}

func TestPopulateKeys(t *testing.T) {
	srcDir := createFakeKeys(t)
	defer os.RemoveAll(srcDir)

	dstDir, err := ioutil.TempDir("", "publish-test-keys-dst")
	if err != nil {
		t.Fatalf("Couldn't create test destination directory.")
	}
	defer os.RemoveAll(dstDir)

	err = populateKeys(dstDir, srcDir)
	if err != nil {
		t.Fatalf("Error returned from populate keys: %v", err)
	}

	for _, k := range keySet {
		_, err := os.Stat(filepath.Join(dstDir, k))
		if err != nil {
			t.Fatalf("Couldn't stat destination file '%s' %v", k, err)
		}
	}
}

func TestInitRepo(t *testing.T) {
	srcDir := createFakeKeys(t)
	defer os.RemoveAll(srcDir)

	f, err := os.Create(filepath.Join(srcDir, rootJSONName))
	if err != nil {
		t.Fatalf("Couldn't create fake root json manifest %v", err)
	}
	f.Close()

	repoDir, err := ioutil.TempDir("", "publish-test-repo")
	if err != nil {
		t.Fatalf("Couldn't create test repo directory.")
	}
	defer os.RemoveAll(repoDir)

	checkPaths := []string{filepath.Join(repoDir, "repository", "root.json")}
	for _, k := range keySet {
		checkPaths = append(checkPaths, filepath.Join(repoDir, "keys", k))
	}

	_, err = initRepo(repoDir, srcDir)
	if err != nil {
		t.Fatalf("Repo init returned error %v", err)
	}

	for _, path := range checkPaths {
		_, err := os.Stat(path)
		if err != nil {
			t.Fatalf("Expected path '%s' had error %v", path, err)
		}
	}
}

func TestAddTUFFile(t *testing.T) {
	storePath, err := ioutil.TempDir("", "publish-test-repo")
	if err != nil {
		t.Fatalf("Couldn't creating test directory %v", err)
	}

	store := tuf.FileSystemStore(storePath, func(role string, confirm bool) ([]byte, error) { return []byte(""), nil })
	repo, err := tuf.NewRepo(store, "sha512")
	if err != nil {
		t.Fatalf("Repository couldn't be created")
	}

	err = repo.Init(true)
	if err != nil {
		t.Fatalf("Error initializing repository %v", err)
	}
	defer os.RemoveAll(storePath)

	keys := []string{"root", "timestamp", "snapshot", "targets"}

	for _, key := range keys {
		_, err = repo.GenKey(key)
		if err != nil {
			t.Fatalf("Error generating key '%s' %v", key, err)
		}
	}

	rf, _ := writeRandFile(t, 8193)
	defer os.Remove(rf)

	targetName := "test-test"
	err = addTUFFile(repo, storePath, rf, "test-test")

	if err != nil {
		t.Fatalf("Problem adding repo file %v", err)
	}

	contents, err := os.Open(filepath.Join(storePath, "repository", "targets"))
	if err != nil {
		t.Fatalf("Unable to read targets directory %v", err)
	}

	found := false
	contentList, err := contents.Readdir(0)
	if err != nil {
		t.Fatalf("Couldn't read targets directory")
	}

	for _, info := range contentList {
		n := info.Name()
		if strings.Contains(n, targetName) &&
			strings.LastIndex(n, targetName) == len(n)-len(targetName) {
			found = true
			break
		}
	}
	if !found {
		t.Fatalf("Didn't find expected file")
	}

	// do a basic sanity check that a merkle element is included in the
	// targets field
	targs, err := os.Open(filepath.Join(storePath, "repository", "targets.json"))
	if err != nil {
		t.Fatalf("Couldn't open targets metadata %v", err)
	}
	defer targs.Close()
	buf := make([]byte, 512*1024)
	l, err := targs.Read(buf)
	if err != nil {
		t.Fatalf("Couldn't read targets metadata %v", err)
	}
	if l == len(buf) {
		t.Log("Metadata filed entire buffer")
	}

	bufStr := string(buf)
	if !strings.Contains(bufStr, "{\"custom\":{\"merkle\"") {
		t.Fatalf("Targets JSON doesn't contain merkle entry")
	}
}

func TestAddBlob(t *testing.T) {
	storePath, err := ioutil.TempDir("", "publish-test-repo")
	if err != nil {
		t.Fatalf("Couldn't creating test directory %v", err)
	}

	store := tuf.FileSystemStore(storePath, func(role string, confirm bool) ([]byte, error) { return []byte(""), nil })
	repo, err := tuf.NewRepo(store, "sha512")
	if err != nil {
		t.Fatalf("Repository couldn't be created")
	}

	err = repo.Init(true)
	if err != nil {
		t.Fatalf("Error initializing repository %v", err)
	}

	defer os.RemoveAll(storePath)
	rf, _ := writeRandFile(t, 8193)
	defer os.Remove(rf)

	addRegFile(rf, storePath)
	blobs, err := os.Open(filepath.Join(storePath, "repository", "blobs"))
	if err != nil {
		t.Fatalf("Couldn't open blobs directory for reading %v", err)
	}
	defer blobs.Close()

	files, err := blobs.Readdir(0)
	if err != nil {
		t.Fatalf("Error reading blobs directory %v", err)
	}
	if len(files) != 1 {
		t.Fatalf("Unexpected number of blobs in blobs directory")
	}

	// TODO(jmatt) Verify name is merkle root?
}

func writeRandFile(t *testing.T, size int) (string, []byte) {
	fileContent := make([]byte, size)
	rand.Seed(time.Now().UnixNano())
	rand.Read(fileContent)

	src, err := ioutil.TempFile("", "publish-test")
	if err != nil {
		t.Fatalf("Could not make temporary source file, %v", err)
	}

	defer src.Close()
	if _, err = src.Write(fileContent); err != nil {
		t.Fatalf("Unable to write to temp file %v", err)
	}
	return src.Name(), fileContent
}

func createFakeKeys(t *testing.T) string {
	srcDir, err := ioutil.TempDir("", "publish-test-keys-src")
	if err != nil {
		t.Fatalf("Couldn't create test source directory.")
	}
	for _, k := range keySet {
		f, err := os.OpenFile(filepath.Join(srcDir, k), os.O_RDWR|os.O_CREATE, 0666)
		if err != nil {
			t.Fatalf("Unable to create source file '%s' %v", k, err)
		}
		f.Close()
	}
	return srcDir
}
