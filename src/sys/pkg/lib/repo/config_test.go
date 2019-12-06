// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package repo

import (
	"encoding/hex"
	"encoding/json"
	"errors"
	"reflect"
	"testing"

	tuf_data "github.com/flynn/go-tuf/data"
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

type keyCase typeAndValue
type testCase struct {
	name     string
	keys     []keyCase
	expected []KeyConfig
	err      error
}

var hexDecodingErr = errors.New("failed to decode string as hex")

func TestGetRootKeys(t *testing.T) {
	cases := []testCase{
		{
			name: "basic",
			keys: []keyCase{
				{
					Type:  tuf_data.KeyTypeEd25519,
					Value: "abc123",
				},
				{
					Type:  tuf_data.KeyTypeEd25519,
					Value: "321cba",
				},
			},
			expected: []KeyConfig{
				{
					ED25519Key: "abc123",
				},
				{
					ED25519Key: "321cba",
				},
			},
			err: nil,
		},
		{
			name: "unkown key type",
			keys: []keyCase{
				{
					Type:  tuf_data.KeyTypeEd25519,
					Value: "abc123",
				},
				{
					Type:  "unknown",
					Value: "321cba",
				},
			},
			expected: nil,
			err:      unexpectedKeyTypeError{Type: "unknown"},
		},
		{
			name: "non-hex key",
			keys: []keyCase{
				{
					Type:  tuf_data.KeyTypeEd25519,
					Value: "odd", // Hex string values must att least be even-numbered in length.
				},
			},
			expected: nil,
			err:      hexDecodingErr,
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			actual, err := getActualKeys(c.keys)
			if !reflect.DeepEqual(err, c.err) {
				t.Errorf("unexpected error: %v\nactual: %v\n", c.err, err)
				return
			}
			if !reflect.DeepEqual(actual, c.expected) {
				t.Errorf("expected:\n%v\n\nactual:\n%v\n", c.expected, actual)
			}
		})
	}
}

func getActualKeys(ks []keyCase) ([]KeyConfig, error) {
	root := tuf_data.NewRoot()
	root.Roles["root"] = &tuf_data.Role{}

	for _, k := range ks {
		key, err := tufKey(k)
		if err != nil {
			return nil, err
		}
		root.AddKey(key)
		root.Roles["root"].AddKeyIDs(key.IDs())
	}
	return GetRootKeys(root)
}

func tufKey(k keyCase) (*tuf_data.Key, error) {
	hexVal, err := hex.DecodeString(k.Value)
	if err != nil {
		return nil, hexDecodingErr
	}
	return &tuf_data.Key{
		Type:   k.Type,
		Scheme: "scheme",
		Value:  tuf_data.KeyValue{Public: tuf_data.HexBytes(hexVal)},
	}, nil
}
