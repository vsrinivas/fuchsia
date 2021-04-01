// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pmhttp

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"math"
	"net/http"

	tuf_data "github.com/theupdateframework/go-tuf/data"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/lib/repo"
)

var (
	OutOfRangeError = errors.New("out of range error")
)

type ConfigServer struct {
	rootKeyFetcher func() []byte
	encryptionKey  string
}

func NewConfigServer(rootKeyFetcher func() []byte, encryptionKey string) *ConfigServer {
	return &ConfigServer{rootKeyFetcher: rootKeyFetcher, encryptionKey: encryptionKey}
}

type Config struct {
	ID            string
	RepoURL       string
	BlobRepoURL   string
	RatePeriod    int
	RootKeys      []repo.KeyConfig
	RootVersion   uint32 `json:"rootVersion,omitempty"`
	RootThreshold uint32 `json:"rootThreshold,omitempty"`
	StatusConfig  struct {
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

	cfg, err := c.parseConfig(repoUrl)
	if err != nil {
		log.Printf("%s", err)
		w.WriteHeader(http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(cfg)
}

func (c *ConfigServer) parseConfig(repoUrl string) (Config, error) {
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

	root, err := func() (tuf_data.Root, error) {
		var signed tuf_data.Signed
		var root tuf_data.Root
		if err := json.Unmarshal(c.rootKeyFetcher(), &signed); err != nil {
			return root, err
		}
		if err := json.Unmarshal(signed.Signed, &root); err != nil {
			return root, err
		}
		return root, nil
	}()
	if err != nil {
		return Config{}, fmt.Errorf("root.json parsing error: %w", err)
	}

	cfg.RootKeys, err = repo.GetRootKeys(&root)
	if err != nil {
		return Config{}, fmt.Errorf("could not get root keys from root.json: %w", err)
	}
	cfg.RootVersion, err = intToUint32(root.Version)
	if err != nil {
		return Config{}, fmt.Errorf("error parsing root version: %w", err)
	}

	if rootRole, ok := root.Roles["root"]; ok {
		cfg.RootThreshold, err = intToUint32(rootRole.Threshold)
		if err != nil {
			return Config{}, fmt.Errorf("error parsing root threshold: %w", err)
		}
	}

	return cfg, nil
}

func intToUint32(x int) (uint32, error) {
	if x < 0 || x > math.MaxUint32 {
		return 0, OutOfRangeError
	}

	return uint32(x), nil
}
