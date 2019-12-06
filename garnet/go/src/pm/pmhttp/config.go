// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pmhttp

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"

	tuf_data "github.com/flynn/go-tuf/data"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/lib/repo"
)

type ConfigServer struct {
	rootKeyFetcher func() []byte
	encryptionKey  string
}

func NewConfigServer(rootKeyFetcher func() []byte, encryptionKey string) *ConfigServer {
	return &ConfigServer{rootKeyFetcher: rootKeyFetcher, encryptionKey: encryptionKey}
}

type Config struct {
	ID           string
	RepoURL      string
	BlobRepoURL  string
	RatePeriod   int
	RootKeys     []repo.KeyConfig
	StatusConfig struct {
		Enabled bool
	}
	Auto    bool
	BlobKey *struct {
		Data [32]uint8
	}
}

func (c *ConfigServer) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	var scheme = "http://"
	if r.TLS != nil {
		scheme = "https://"
	}

	repoUrl := fmt.Sprintf("%s%s", scheme, r.Host)

	cfg := Config{
		ID:          repoUrl,
		RepoURL:     repoUrl,
		BlobRepoURL: repoUrl + "/blobs",
		RatePeriod:  60,
		StatusConfig: struct {
			Enabled bool
		}{
			Enabled: true,
		},
		Auto: true,
	}

	if c.encryptionKey != "" {
		keyBytes, err := ioutil.ReadFile(c.encryptionKey)
		if err != nil {
			log.Fatal(err)
		}
		if len(keyBytes) != 32 {
			log.Fatalf("encryption key %s of improper size", c.encryptionKey)
		}
		cfg.BlobKey = &struct{ Data [32]uint8 }{}
		copy(cfg.BlobKey.Data[:], keyBytes)
	}

	var err error
	cfg.RootKeys, err = func() ([]repo.KeyConfig, error) {
		var signed tuf_data.Signed
		if err := json.Unmarshal(c.rootKeyFetcher(), &signed); err != nil {
			return nil, err
		}
		var root tuf_data.Root
		if err := json.Unmarshal(signed.Signed, &root); err != nil {
			return nil, err
		}
		return repo.GetRootKeys(&root)
	}()
	if err != nil {
		log.Printf("root.json parsing error: %s", err)
		w.WriteHeader(http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(cfg)
}
