// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package repo

import (
	"encoding/json"
	"reflect"
	"sort"
	"testing"

	tuf_data "github.com/flynn/go-tuf/data"
)

func TestMarshaling(t *testing.T) {
	goldenCfg := Config{
		URL: "fuchsia-pkg://example.com",
		RootKeys: []KeyConfig{
			{
				ED25519Key: "8e70ed31f117087a08ad23e00c2e1c353bf76fc0e0ac1aac334336e2b83ee7f4",
			},
		},
		Mirrors: []MirrorConfig{{
			URL:       "https://example.com/repo",
			Subscribe: true,
		}},
		RootVersion:   1,
		RootThreshold: 1,
	}

	cfgStr := `
    {
      "repo_url": "fuchsia-pkg://example.com",
      "root_keys": [
        {
          "type": "ed25519",
          "value": "8e70ed31f117087a08ad23e00c2e1c353bf76fc0e0ac1aac334336e2b83ee7f4"
        }
      ],
      "mirrors": [
        {
          "mirror_url": "https://example.com/repo",
          "subscribe": true
        }
      ]
  }`

	var cfg Config
	if err := json.Unmarshal([]byte(cfgStr), &cfg); err != nil {
		t.Fatalf("could not unmarshal golden config string: %v", err)
	}

	if !reflect.DeepEqual(cfg, goldenCfg) {
		t.Fatalf("expected\n%#v\nand\n%#v\nto be equal", cfg, goldenCfg)
	}
}

func TestGetRootKeys(t *testing.T) {
	rootJSONStr := `
{
  "_type": "root",
  "keys": {
    "33efe3720e278de9ac57c810c1be71b9ef4dd49c0951edd9a2f9a39af08b5776": {
      "keyid_hash_algorithms": [
       "sha256"
      ],
      "keytype": "ed25519",
      "keyval": {
        "public": "8e70ed31f117087a08ad23e00c2e1c353bf76fc0e0ac1aac334336e2b83ee7f4"
      },
      "scheme": "ed25519"
    },
    "8bcc00bc3575eea5fb608dea6a521845d4f287d2fa80baa40a902f1f3ec7911e": {
      "keyid_hash_algorithms": [
        "sha256"
      ],
     "keytype": "ed25519",
     "keyval": {
        "public": "be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307"
      },
      "scheme": "ed25519"
    },
    "c919b6e358fdb4ed062311ac5cebd44787d0f7ae9e5a5a213929dd4e3cde07c4": {
      "keyid_hash_algorithms": [
        "sha256"
      ],
      "keytype": "ed25519",
      "keyval": {
        "public": "be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307"
      },
      "scheme": "ed25519"
    },
    "f6ee2f092af683f6c1d8cbf477c2a8b7e01cc496fb4324150a172a54f514e4e7": {
      "keyid_hash_algorithms": [
        "sha256"
      ],
      "keytype": "ed25519",
      "keyval": {
        "public": "8e70ed31f117087a08ad23e00c2e1c353bf76fc0e0ac1aac334336e2b83ee7f4"
      },
      "scheme": "ed25519"
    }
  },
  "roles": {
    "root": {
      "keyids": [
        "8bcc00bc3575eea5fb608dea6a521845d4f287d2fa80baa40a902f1f3ec7911e",
        "c919b6e358fdb4ed062311ac5cebd44787d0f7ae9e5a5a213929dd4e3cde07c4",
        "f6ee2f092af683f6c1d8cbf477c2a8b7e01cc496fb4324150a172a54f514e4e7"
      ]
    },
    "other": {
      "keyids": [
        "33efe3720e278de9ac57c810c1be71b9ef4dd49c0951edd9a2f9a39af08b5776",
        "f6ee2f092af683f6c1d8cbf477c2a8b7e01cc496fb4324150a172a54f514e4e7"
      ]
    }
  }
}
`
	var root tuf_data.Root
	if err := json.Unmarshal([]byte(rootJSONStr), &root); err != nil {
		t.Fatalf("failed to unmarshal root metadata: %v", err)
	}
	actual, err := GetRootKeys(&root)
	if err != nil {
		t.Fatalf("failed to derive root keys: %v", err)
	}
	sort.Slice(actual, func(i, j int) bool {
		return actual[i].ED25519Key <= actual[j].ED25519Key
	})
	expected := []KeyConfig{
		{
			ED25519Key: "8e70ed31f117087a08ad23e00c2e1c353bf76fc0e0ac1aac334336e2b83ee7f4",
		},
		{
			ED25519Key: "be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307",
		},
	}
	if !reflect.DeepEqual(expected, actual) {
		t.Errorf("unexpected keys:\nexpected: %v\nactual: %v", expected, actual)
	}
}
