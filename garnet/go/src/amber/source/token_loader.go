// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source

import (
	"amber/atonce"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"sync"
	"time"
	"unicode/utf8"
)

type TokenRecord struct {
	ProviderType string `json:"auth_provider_type"`
	ProfileId    string `json:"user_profile_id"`
	RefreshToken string `json:"refresh_token"`
}

type TokenStore []TokenRecord

type TokenLoader struct {
	AuthDir string
	watcher tokenObserver
}

type tokenCallback func(string, string)

const (
	oauthClientId = "934259141868-rejmm4ollj1bs7th1vg2ur6antpbug79.apps.googleusercontent.com"
)

var defaultTokenLoader = &TokenLoader{AuthDir: "/data/auth"}

var tknStorePat = regexp.MustCompile(".+_token_store\\.json$")

func (t *TokenLoader) ReadToken(f tokenCallback, clientId string) {
	t.watcher.observe(clientId, f)
	atonce.Do("token-loader-read-token", "", t.read)
}

func (t *TokenLoader) read() error {
	log.Printf("token_loader: loading auth tokens")
	immediate := make(chan struct{}, 1)
	immediate <- struct{}{}
	defer close(immediate)
	timer := time.NewTicker(5 * time.Second)
	defer timer.Stop()

	// loop until we succeed. This is potentially expensive because if no one
	// ever logs in we'll spin forever. however we have no signaling to
	// understand that someone has logged in. As a result there's no way to
	// know what an acceptable timeout period would be when we could give up
	for {
		select {
		case <-immediate:
		case <-timer.C:
		}

		d, err := os.OpenFile(t.AuthDir, os.O_RDONLY, os.ModeDir)
		if err != nil {
			if !os.IsNotExist(err) {
				log.Printf("token_loader: unexpected error open auth directory: %s", err)
			}
			continue
		}

		path, err := findAuthTokenFile(d)
		d.Close()
		if err != nil {
			if !os.IsNotExist(err) {
				log.Printf("token_loader: error finding token file: %s", err)
			}
			continue
		}

		ts, err := readTokenFile(path)
		if err != nil {
			log.Printf("token_loader: json decode error: %s", err)
			continue
		}

		err = decodeTokens(ts)
		if err != nil {
			continue
		}

		for i := range ts {
			go t.watcher.update(oauthClientId, ts[i].RefreshToken)
		}

		break
	}
	return nil
}

func findAuthTokenFile(dir *os.File) (string, error) {
	files, err := dir.Readdir(-1)
	if err != nil {
		return "", err
	}

	for _, file := range files {
		if !tknStorePat.MatchString(file.Name()) {
			continue
		}
		log.Printf("token_loader: found matching file %s", file.Name())
		fp := filepath.Join(dir.Name(), file.Name())
		return fp, nil
	}

	return "", os.ErrNotExist
}

func decodeTokens(ts TokenStore) error {
	for i := range ts {
		t, err := base64.StdEncoding.DecodeString(ts[i].RefreshToken)
		if err != nil {
			return err
		}

		if !utf8.Valid(t) {
			return fmt.Errorf("token is invalid, not UTF-8")
		}

		ts[i].RefreshToken = string(t)
	}
	return nil
}

func readTokenFile(path string) (TokenStore, error) {
	tokenFile, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer tokenFile.Close()

	var ts TokenStore
	if err := json.NewDecoder(tokenFile).Decode(&ts); err != nil {
		return nil, err
	}
	return ts, nil
}

type tokenObserver struct {
	mu        sync.Mutex
	observers map[string][]tokenCallback
}

func (t *tokenObserver) observe(key string, f tokenCallback) {
	t.mu.Lock()
	defer t.mu.Unlock()

	if t.observers == nil {
		t.observers = make(map[string][]tokenCallback)
	}

	t.observers[key] = append(t.observers[key], f)
}

func (t *tokenObserver) update(key string, refreshTkn string) {
	t.mu.Lock()
	obs := t.observers[key]
	delete(t.observers, key)
	t.mu.Unlock()

	for _, ob := range obs {
		ob(key, refreshTkn)
	}
}
