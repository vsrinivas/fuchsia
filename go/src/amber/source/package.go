// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source

import (
	"encoding/json"
	"fmt"
	"os"
	"time"

	tuf "github.com/flynn/go-tuf/client"
	tuf_data "github.com/flynn/go-tuf/data"
)

type RemoteStoreError struct {
	error
}

type IOError struct {
	error
}

func NewTUFClient(url string, path string) (*tuf.Client, tuf.LocalStore, error) {
	tufStore, err := tuf.FileLocalStore(path)
	if err != nil {
		return nil, nil, IOError{fmt.Errorf("amber: couldn't open datastore %s\n", err)}
	}

	server, err := tuf.HTTPRemoteStore(url, nil, nil)
	if err != nil {
		return nil, nil, RemoteStoreError{fmt.Errorf("amber: server address not understood %s\n", err)}
	}

	return tuf.NewClient(tufStore, server), tufStore, nil
}

func InitNewTUFClient(url string, path string, keys []*tuf_data.Key, ticker *TickGenerator) (*tuf.Client, tuf.LocalStore, error) {
	client, store, err := NewTUFClient(url, path)

	defer ticker.Done()
	if err != nil {
		return nil, nil, err
	}

	needs, err := NeedsInit(store)
	if err != nil {
		return nil, nil, err
	}

	if needs {
		if err = InitClient(client, keys, ticker); err != nil {
			return nil, nil, err
		}
	}

	return client, store, nil
}

func LoadKeys(path string) ([]*tuf_data.Key, error) {
	f, err := os.Open(path)
	defer f.Close()
	if err != nil {
		return nil, err
	}

	var keys []*tuf_data.Key
	err = json.NewDecoder(f).Decode(&keys)
	return keys, err
}

func NeedsInit(s tuf.LocalStore) (bool, error) {
	meta, err := s.GetMeta()
	if err != nil {
		return false, err
	}

	_, ok := meta["root.json"]
	return !ok, nil
}

func InitClient(c *tuf.Client, keys []*tuf_data.Key, clock *TickGenerator) error {
	var err error = nil
	for {
		if !clock.AwaitTick() {
			if err != nil {
				return RemoteStoreError{fmt.Errorf(
					"Timed out trying to initialize, error from last attempt: %s", err)}
			} else {
				return RemoteStoreError{fmt.Errorf("Timed out trying to initialize")}
			}
		}

		err := c.Init(keys, len(keys))
		if err == nil {
			break
		}
	}

	return nil
}

func InitBackoff(cur time.Duration) time.Duration {
	minDelay := 1 * time.Second
	maxStep := 30 * time.Second
	maxDelay := 5 * time.Minute

	if cur < time.Second {
		return minDelay
	}

	if cur > maxStep {
		cur += maxStep
	} else {
		cur += cur
	}

	if cur > maxDelay {
		cur = maxDelay
	}
	return cur
}
