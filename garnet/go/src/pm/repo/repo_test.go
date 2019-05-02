// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package repo

import (
	"bytes"
	"crypto/rand"
	"crypto/sha512"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	"fuchsia.googlesource.com/merkle"
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

var roleJsons = []string{"root.json", "timestamp.json", "targets.json", "snapshot.json"}
var merklePat = regexp.MustCompile("^[0-9a-f]{64}$")

func TestInitRepo(t *testing.T) {
	repoDir, err := ioutil.TempDir("", "publish-test-repo")
	if err != nil {
		t.Fatalf("Couldn't create test repo directory.")
	}
	defer os.RemoveAll(repoDir)

	r, err := New(repoDir)
	if err != nil {
		t.Fatalf("Repo init returned error %v", err)
	}
	if err := r.Init(); err != nil {
		t.Fatal(err)
	}
	if err := r.GenKeys(); err != nil {
		t.Fatal(err)
	}

	for _, rolejson := range roleJsons {
		path := filepath.Join(repoDir, "keys", rolejson)
		if _, err := os.Stat(path); err != nil {
			t.Fatal(err)
		}
	}
}

func TestAddPackage(t *testing.T) {
	keysPath, err := ioutil.TempDir("", "publish-test-keys")
	if err != nil {
		t.Fatalf("Couldn't creating test directory %v", err)
	}
	defer os.RemoveAll(keysPath)

	repoDir, err := ioutil.TempDir("", "publish-test-repo")
	if err != nil {
		t.Fatalf("Couldn't create test repo directory.")
	}
	defer os.RemoveAll(repoDir)

	r, err := New(repoDir)
	if err != nil {
		t.Fatalf("Repo init returned error %v", err)
	}
	if err := r.Init(); err != nil {
		t.Fatal(err)
	}
	if err := r.GenKeys(); err != nil {
		t.Fatal(err)
	}
	for _, rolejson := range roleJsons {
		path := filepath.Join(repoDir, "keys", rolejson)
		if _, err := os.Stat(path); err != nil {
			t.Fatal(err)
		}
	}

	targetName := "test-test"
	err = r.AddPackage("test-test", io.LimitReader(rand.Reader, 8193), "")

	if err != nil {
		t.Fatalf("Problem adding repo file %v", err)
	}

	if err = r.CommitUpdates(true); err != nil {
		t.Fatalf("Failure commiting update %s", err)
	}

	// Check for rolejsons and consistent snapshots:
	for _, rolejson := range roleJsons {
		b, err := ioutil.ReadFile(filepath.Join(repoDir, "repository", rolejson))
		if err != nil {
			t.Fatal(err)
		}
		// timestamp doesn't get a consistent snapshot, as it is the entrypoint
		if rolejson == "timestamp.json" {
			continue
		}
		sum512 := sha512.Sum512(b)
		path := filepath.Join(repoDir, "repository", fmt.Sprintf("%x.%s", sum512, rolejson))
		if _, err := os.Stat(path); err != nil {
			t.Fatal(err)
		}
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
	repoDir, err := ioutil.TempDir("", "publish-test-repo")
	if err != nil {
		t.Fatalf("Couldn't create test repo directory.")
	}
	defer os.RemoveAll(repoDir)

	repo, err := New(repoDir)
	if err != nil {
		t.Fatalf("Repo init returned error %v", err)
	}

	res, n, err := repo.AddBlob("", io.LimitReader(rand.Reader, 8193))
	if err != nil {
		t.Fatal(err)
	}
	if want := int64(8193); n != want {
		t.Fatalf("got %d, want %d", n, want)
	}
	blobs, err := os.Open(filepath.Join(repoDir, "repository", "blobs"))
	if err != nil {
		t.Fatalf("Couldn't open blobs directory for reading %v", err)
	}
	files, err := blobs.Readdir(0)
	blobs.Close()
	if err != nil {
		t.Fatalf("Error reading blobs directory %v", err)
	}
	if len(files) != 1 {
		t.Fatalf("Unexpected number of blobs in blobs directory")
	}
	if res != files[0].Name() {
		t.Fatalf("computed merkle: %s, filename: %s", res, files[0].Name())
	}

	blobPath := filepath.Join(repoDir, "repository", "blobs", res)
	b, err := ioutil.ReadFile(blobPath)
	if err != nil {
		t.Fatal(err)
	}
	var tree merkle.Tree
	if _, err := tree.ReadFrom(bytes.NewReader(b)); err != nil {
		t.Fatal(err)
	}
	mr := hex.EncodeToString(tree.Root())
	if res != mr {
		t.Fatalf("got %s, want %s", res, mr)
	}

	if err := os.Remove(blobPath); err != nil {
		t.Fatal(err)
	}

	// Test adding a blob with a pre-computed name
	_, n, err = repo.AddBlob(mr, bytes.NewReader(b))
	if err != nil {
		t.Fatal(err)
	}

	if want := int64(len(b)); n != want {
		t.Fatalf("got %d, want %d", n, want)
	}

	if _, err := os.Stat(blobPath); err != nil {
		t.Fatal(err)
	}
}
