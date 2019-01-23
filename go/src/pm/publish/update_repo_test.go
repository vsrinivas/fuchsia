// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package publish

import (
	"bytes"
	"crypto/rand"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	tuf "github.com/flynn/go-tuf"
)

type targetsFile struct {
	Signed struct {
		Targets map[string]struct {
			Custom struct {
				Merkle string `json:"merkle"`
			} `json:"custom"`
		} `json:"targets"`
	} `json:"signed"`
}

var merklePat = regexp.MustCompile("^[0-9a-f]{64}$")

func TestCopyFile(t *testing.T) {
	fileSize := 8193
	src := writeRandFile(t, fileSize)
	defer os.Remove(src)
	fileContent, err := ioutil.ReadFile(src)
	if err != nil {
		t.Fatal(err)
	}

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
	if err != nil {
		t.Fatal(err)
	}

	if len(copyContent) != fileSize || err != nil {
		t.Fatalf("Copied file could not be read")
	}

	if bytes.Compare(copyContent, fileContent) != 0 {
		t.Fatalf("Copied file content does not match")
	}
}

func TestCopyMakingDirs(t *testing.T) {
	fileSize := 8193
	src := writeRandFile(t, fileSize)
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
	keysPath, err := ioutil.TempDir("", "publish-test-keys")
	if err != nil {
		t.Fatalf("Couldn't creating test directory %v", err)
	}
	defer os.RemoveAll(keysPath)

	genKeys(keysPath, t)

	repoDir, err := ioutil.TempDir("", "publish-test-repo")
	if err != nil {
		t.Fatalf("Couldn't create test repo directory.")
	}
	defer os.RemoveAll(repoDir)

	checkPaths := []string{filepath.Join(repoDir, "repository", "root.json")}
	for _, k := range keySet {
		checkPaths = append(checkPaths, filepath.Join(repoDir, "keys", k))
	}

	_, err = InitRepo(repoDir, keysPath)
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
	keysPath, err := ioutil.TempDir("", "publish-test-keys")
	if err != nil {
		t.Fatalf("Couldn't creating test directory %v", err)
	}
	defer os.RemoveAll(keysPath)

	genKeys(keysPath, t)

	repoDir, err := ioutil.TempDir("", "publish-test-repo")
	if err != nil {
		t.Fatalf("Couldn't create test repo directory.")
	}
	defer os.RemoveAll(repoDir)

	checkPaths := []string{filepath.Join(repoDir, "repository", "root.json")}
	for _, k := range keySet {
		checkPaths = append(checkPaths, filepath.Join(repoDir, "keys", k))
	}

	amberRepo, err := InitRepo(repoDir, keysPath)
	if err != nil {
		t.Fatalf("Repo init returned error %v", err)
	}

	targetName := "test-test"
	err = amberRepo.AddPackage("test-test", io.LimitReader(rand.Reader, 8193))

	if err != nil {
		t.Fatalf("Problem adding repo file %v", err)
	}

	if err = amberRepo.CommitUpdates(true); err != nil {
		t.Fatalf("Failure commiting update %s", err)
	}

	contents, err := os.Open(filepath.Join(repoDir, "repository", "targets"))
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
	targs, err := os.Open(filepath.Join(repoDir, "repository", "targets.json"))
	if err != nil {
		t.Fatalf("Couldn't open targets metadata %v", err)
	}
	defer targs.Close()

	var targets targetsFile
	decoder := json.NewDecoder(targs)
	err = decoder.Decode(&targets)
	if err != nil {
		t.Fatalf("Couldn't decode targets metadata %v", err)
	}

	if len(targets.Signed.Targets) == 0 {
		t.Fatalf("Targets file contains no targets")
	}

	for _, target := range targets.Signed.Targets {
		if !merklePat.MatchString(target.Custom.Merkle) {
			t.Fatalf("Targets JSON contains invalid merkle entry: %v", target.Custom.Merkle)
		}
	}
}

func TestAddBlob(t *testing.T) {
	keysPath, err := ioutil.TempDir("", "publish-test-keys")
	if err != nil {
		t.Fatalf("Couldn't creating test directory %v", err)
	}
	defer os.RemoveAll(keysPath)

	genKeys(keysPath, t)

	repoDir, err := ioutil.TempDir("", "publish-test-repo")
	if err != nil {
		t.Fatalf("Couldn't create test repo directory.")
	}
	defer os.RemoveAll(repoDir)

	checkPaths := []string{filepath.Join(repoDir, "repository", "root.json")}
	for _, k := range keySet {
		checkPaths = append(checkPaths, filepath.Join(repoDir, "keys", k))
	}

	repo, err := InitRepo(repoDir, keysPath)
	if err != nil {
		t.Fatalf("Repo init returned error %v", err)
	}
	defer os.RemoveAll(repoDir)

	repo.AddBlob("", io.LimitReader(rand.Reader, 8193))
	blobs, err := os.Open(filepath.Join(repoDir, "repository", "blobs"))
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

func writeRandFile(t *testing.T, size int) string {
	dst, err := ioutil.TempFile("", "publish-test")
	if err != nil {
		t.Fatalf("Could not make temporary file, %v", err)
	}
	defer dst.Close()
	if _, err = io.Copy(dst, io.LimitReader(rand.Reader, int64(size))); err != nil {
		t.Fatalf("Unable to write to temp file %v", err)
	}
	return dst.Name()
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

// genKeys uses go-tuf client methods directly to create a set of keys which
// our wrapper client is expected to ingest
func genKeys(keysDst string, t *testing.T) {
	// use the TUF library directly to create a set of keys and empty root
	// manifest
	storePath, err := ioutil.TempDir("", "publish-test-genkeys")
	if err != nil {
		t.Fatalf("Couldn't creating test directory %v", err)
	}
	defer os.RemoveAll(storePath)

	store := tuf.FileSystemStore(storePath, func(role string, confirm bool) ([]byte, error) { return []byte(""), nil })
	tufRepo, err := tuf.NewRepo(store, "sha512")
	if err != nil {
		t.Fatalf("Repository couldn't be created")
	}

	err = tufRepo.Init(true)
	if err != nil {
		t.Fatalf("Error initializing repository %v", err)
	}

	// create all the keys
	for _, k := range []string{"root", "timestamp", "targets", "snapshot"} {
		_, err = tufRepo.GenKey(k)
		if err != nil {
			fmt.Printf("Error creating key %s: %s\n", k, err)
		}
		filename := k + ".json"
		err = copyFile(filepath.Join(keysDst, filename),
			filepath.Join(storePath, "keys", filename))
		if err != nil {
			t.Fatalf("Failed to copy key to output path %v", err)
		}
	}

	// copy the root.json, which is the manifest for the empty repo into the keys
	// directory. The InitRepo method will want to ingest this.
	err = copyFile(filepath.Join(keysDst, rootManifest),
		filepath.Join(storePath, "staged", "root.json"))

	if err != nil {
		t.Fatalf("Couldn't copy root json manifest %v", err)
	}
}
