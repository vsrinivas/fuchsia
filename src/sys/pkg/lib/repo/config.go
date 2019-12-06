// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package repo

import (
	"encoding/json"
	"fmt"

	tuf_data "github.com/flynn/go-tuf/data"
)

// Config is a struct that mirrors an associated FIDL table
// definition in //sdk/fidl/fuchsia.pkg/repo.fidl. Documentation is as in
// that file. Ditto for the types that comprise its definition.
//
// Keep these in sync with their repo.fidl counterparts as well as the custom
//
type Config struct {
	URL              string         `json:"repo_url"`
	RootKeys         []KeyConfig    `json:"root_keys"`
	Mirrors          []MirrorConfig `json:"mirrors"`
	UpdatePackageURL string         `json:"update_package_url"`
}

type MirrorConfig struct {
	URL       string `json:"mirror_url"`
	Subscribe bool   `json:"subscribe"`
	BlobURL   string `json:"blob_mirror_url",omitempty`
}

type KeyConfig struct {
	// ED25519Key is a 32-byte, lowercase, hex-encoded key.
	ED25519Key string
}

// We replicate the serialization/deserialization logic given in
// //garnet/lib/rust/fidl_fuchsia_pkg_ext/src/repo.rs;
// Per this logic, we set the BlobURL field in MirrorConfig as omitempty (above),
// and give custom marshaling logic to the key config.
//
// TODO(fxbug.dev/37022): Update the above source reference when the code is
// moved under //src,
//
type typeAndValue struct {
	Type  string `json:"type"`
	Value string `json:"value"`
}

type unexpectedKeyTypeError struct {
	Type string
}

func (err unexpectedKeyTypeError) Error() string {
	return fmt.Sprintf("unsupported key type: %s", err.Type)
}

func (key *KeyConfig) MarshalJSON() ([]byte, error) {
	return json.Marshal(&typeAndValue{
		Type:  tuf_data.KeyTypeEd25519,
		Value: key.ED25519Key,
	})
}

func (key *KeyConfig) UnmarshalJSON(data []byte) error {
	var tv typeAndValue
	if err := json.Unmarshal(data, &tv); err != nil {
		return err
	}

	switch tv.Type {
	case tuf_data.KeyTypeEd25519:
		key.ED25519Key = tv.Value
		return nil
	default:
		return unexpectedKeyTypeError{Type: tv.Type}
	}
}

// GetRootKeys returns the list of public key config objects as read from the
// contents of a repository's root metadata file.
func GetRootKeys(root *tuf_data.Root) ([]KeyConfig, error) {
	var rootKeys []KeyConfig
	for _, k := range root.UniqueKeys()["root"] {
		v := k.Value.Public.String()
		var key KeyConfig
		switch k.Type {
		case tuf_data.KeyTypeEd25519:
			key.ED25519Key = v
		default:
			return nil, unexpectedKeyTypeError{Type: k.Type}
		}
		rootKeys = append(rootKeys, key)
	}
	return rootKeys, nil
}
