// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pmhttp

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"

	tuf_data "github.com/theupdateframework/go-tuf/data"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/lib/repo"
)

type ConfigServerV2 struct {
	rootKeyFetcher func() []byte
}

func NewConfigServerV2(rootKeyFetcher func() []byte) *ConfigServerV2 {
	return &ConfigServerV2{rootKeyFetcher: rootKeyFetcher}
}

func (c *ConfigServerV2) ServeHTTP(w http.ResponseWriter, r *http.Request) {
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

func (c *ConfigServerV2) parseConfig(repoUrl string) (repo.Config, error) {
	cfg := repo.Config{
		URL: repoUrl,
		Mirrors: []repo.MirrorConfig{
			{
				URL:       repoUrl,
				Subscribe: true,
			},
		},
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
		return repo.Config{}, fmt.Errorf("root.json parsing error: %w", err)
	}

	cfg.RootKeys, err = repo.GetRootKeys(&root)
	if err != nil {
		return repo.Config{}, fmt.Errorf("could not get root keys from root.json: %w", err)
	}
	cfg.RootVersion, err = intToUint32(root.Version)
	if err != nil {
		return repo.Config{}, fmt.Errorf("error parsing root version: %w", err)
	}

	if rootRole, ok := root.Roles["root"]; ok {
		cfg.RootThreshold, err = intToUint32(rootRole.Threshold)
		if err != nil {
			return repo.Config{}, fmt.Errorf("error parsing root threshold: %w", err)
		}
	}

	return cfg, nil
}
