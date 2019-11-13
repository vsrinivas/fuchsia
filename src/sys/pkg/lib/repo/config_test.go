// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package repo

import (
	"encoding/json"
	"reflect"
	"testing"
)

func TestMarshaling(t *testing.T) {
	cfg := &Config{
		URL: "fuchsia-pkg://example.com",
		RootKeys: []KeyConfig{
			{
				ED25519Key: "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100",
			},
		},
		Mirrors: []MirrorConfig{{
			URL:       "https://example.com/repo",
			Subscribe: true,
		}},
	}
	goldenCfgStr := `
    {
      "repo_url": "fuchsia-pkg://example.com",
      "root_keys": [
        {
          "type": "ed25519",
          "value": "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100"
        }
      ],
      "mirrors": [
        {
          "mirror_url": "https://example.com/repo",
          "subscribe": true
        }
      ]
  }`

	goldenCfg := &Config{}
	if err := json.Unmarshal([]byte(goldenCfgStr), goldenCfg); err != nil {
		t.Fatalf("could not unmarsal golden config string: %v", err)
	}
	if !reflect.DeepEqual(cfg, goldenCfg) {
		t.Fatalf("expected\n%#v\nand\n%#v\nto be equal", cfg, goldenCfg)
	}
}
