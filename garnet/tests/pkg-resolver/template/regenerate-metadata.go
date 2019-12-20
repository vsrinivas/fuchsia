// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a helper script that regenerates the initial test metadata.

package main

import (
	"encoding/json"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"time"

	tuf "github.com/flynn/go-tuf"
	"github.com/flynn/go-tuf/sign"
)

var expirationDate = time.Date(2100, time.January, 1, 0, 0, 0, 0, time.UTC)

func loadKeys(repo *tuf.Repo, role string, path string) {
	f, err := os.Open(path)
	if err != nil {
		log.Fatal(err)
	}

	var persistedKeys struct {
		Encrypted bool               `json:"encrypted"`
		Data      []*sign.PrivateKey `json:"data"`
	}

	if err := json.NewDecoder(f).Decode(&persistedKeys); err != nil {
		log.Fatal(err)
	}

	for _, key := range persistedKeys.Data {
		if err := repo.AddPrivateKeyWithExpires(role, key, expirationDate); err != nil {
			log.Fatalf("failed to add private key: %s", err)
		}
	}
}

func main() {
	tempdir, err := ioutil.TempDir(".", "")
	if err != nil {
		log.Fatal(err)
	}
	defer os.RemoveAll(tempdir)

	repo, err := tuf.NewRepo(tuf.FileSystemStore(tempdir, nil))
	if err != nil {
		log.Fatalf("failed to create repository", err)
	}

	if err := repo.Init(true); err != nil {
		log.Fatalf("failed init init: %s", err)
	}

	loadKeys(repo, "root", "keys/root.json")
	loadKeys(repo, "targets", "keys/targets.json")
	loadKeys(repo, "snapshot", "keys/snapshot.json")
	loadKeys(repo, "timestamp", "keys/timestamp.json")

	if repo.AddTargetsWithExpires([]string{}, nil, expirationDate); err != nil {
		log.Fatalf("failed to create targets: %s", err)
	}

	if err := repo.SnapshotWithExpires(tuf.CompressionTypeNone, expirationDate); err != nil {
		log.Fatalf("failed to create snapshot: %s", err)
	}

	if err := repo.TimestampWithExpires(expirationDate); err != nil {
		log.Fatalf("failed to create timestamp: %s", err)
	}

	if err := repo.Commit(); err != nil {
		log.Fatalf("failed to commit: %s", err)
	}

	// Delete the old metadata.
	if _, err := os.Stat("repository"); err == nil {
		if err := os.RemoveAll("repository"); err != nil {
			log.Fatal(err)
		}
	}

	if err := os.Rename(filepath.Join(tempdir, "repository"), "repository"); err != nil {
		log.Fatal(err)
	}
}
