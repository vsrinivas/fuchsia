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
)

type ConfigServer struct {
	rootKeyFetcher func() []byte
	encryptionKey  string
}

func NewConfigServer(rootKeyFetcher func() []byte, encryptionKey string) *ConfigServer {
	return &ConfigServer{rootKeyFetcher: rootKeyFetcher, encryptionKey: encryptionKey}
}

func (c *ConfigServer) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	var scheme = "http://"
	if r.TLS != nil {
		scheme = "https://"
	}

	repoUrl := fmt.Sprintf("%s%s", scheme, r.Host)

	cfg := struct {
		ID          string
		RepoURL     string
		BlobRepoURL string
		RatePeriod  int
		RootKeys    []struct {
			Type  string
			Value string
		}
		StatusConfig struct {
			Enabled bool
		}
		Auto    bool
		BlobKey *struct {
			Data [32]uint8
		}
	}{
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

	var keys signedKeys
	if err := json.Unmarshal(c.rootKeyFetcher(), &keys); err != nil {
		log.Printf("root.json parsing error: %s", err)
		w.WriteHeader(http.StatusInternalServerError)
		return
	}

	for _, id := range keys.Signed.Roles.Root.Keyids {
		k := keys.Signed.Keys[id]
		cfg.RootKeys = append(cfg.RootKeys, struct{ Type, Value string }{
			Type:  k.Keytype,
			Value: k.Keyval.Public,
		})
	}
	json.NewEncoder(w).Encode(cfg)
}

type signedKeys struct {
	Signed struct {
		Keys map[string]struct {
			Keytype string
			Keyval  struct {
				Public string
			}
		}
		Roles struct {
			Root struct {
				Keyids []string
			}
			Threshold int
		}
	}
}
