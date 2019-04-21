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
	"os"
	"path/filepath"
)

type ConfigServer struct {
	repoDir       string
	encryptionKey string
}

func NewConfigServer(repoDir, encryptionKey string) *ConfigServer {
	return &ConfigServer{repoDir: repoDir, encryptionKey: encryptionKey}
}

func (c *ConfigServer) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	var scheme = "http://"
	if r.TLS != nil {
		scheme = "https://"
	}

	repoUrl := fmt.Sprintf("%s%s", scheme, r.Host)

	var signedKeys struct {
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
	f, err := os.Open(filepath.Join(c.repoDir, "root.json"))
	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		log.Printf("root.json missing or unreadable: %s", err)
		return
	}
	defer f.Close()
	if err := json.NewDecoder(f).Decode(&signedKeys); err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		log.Printf("root.json parsing error: %s", err)
		return
	}

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

	for _, id := range signedKeys.Signed.Roles.Root.Keyids {
		k := signedKeys.Signed.Keys[id]
		cfg.RootKeys = append(cfg.RootKeys, struct{ Type, Value string }{
			Type:  k.Keytype,
			Value: k.Keyval.Public,
		})
	}
	json.NewEncoder(w).Encode(cfg)
}
