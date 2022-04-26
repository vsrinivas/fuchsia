// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"strconv"
)

//go:embed version_history.json
var versionHistoryBytes []byte
var versions []Version

const versionHistorySchemaId string = "https://fuchsia.dev/schema/version_history-ef02ef45.json"
const versionHistoryName string = "Platform version map"
const versionHistoryType string = "version_history"

type Version struct {
	APILevel    uint64
	ABIRevision uint64
}

type versionHistory struct {
	SchemaId string             `json:"schema_id"`
	Data     versionHistoryData `json:"data"`
}

type versionHistoryData struct {
	Name     string                  `json:"name"`
	Type     string                  `json:"type"`
	Versions []versionHistoryVersion `json:"versions"`
}

type versionHistoryVersion struct {
	APILevel    string `json:"api_level"`
	ABIRevision string `json:"abi_revision"`
}

func parseVersionHistory(b []byte) ([]Version, error) {
	var vh versionHistory

	// Load external JSON of SDK version history
	if err := json.Unmarshal(b, &vh); err != nil {
		return []Version{}, err
	}

	if vh.SchemaId != versionHistorySchemaId {
		return []Version{}, fmt.Errorf("expected schema id %q, not %q", versionHistorySchemaId, vh.SchemaId)
	}

	if vh.Data.Name != versionHistoryName {
		return []Version{}, fmt.Errorf("expected name \"version_history\", not %q", vh.Data.Name)
	}

	if vh.Data.Type != versionHistoryType {
		return []Version{}, fmt.Errorf("expected type \"version_history\", not %q", vh.Data.Type)
	}

	vs := []Version{}
	for _, version := range vh.Data.Versions {
		apiLevel, err := strconv.ParseUint(version.APILevel, 10, 64)
		if err != nil {
			return []Version{}, fmt.Errorf("failed to parse API level as an integer: %w", err)
		}

		abiRevision, err := strconv.ParseUint(version.ABIRevision, 0, 64)
		if err != nil {
			return []Version{}, fmt.Errorf("failed to parse ABI revision as an integer: %w", err)
		}

		vs = append(vs, Version{
			APILevel:    apiLevel,
			ABIRevision: uint64(abiRevision),
		})
	}

	return vs, nil
}

func init() {
	v, err := parseVersionHistory(versionHistoryBytes)
	if err != nil {
		panic(fmt.Sprintf("failed to parse version_history.json: %s", err))
	}
	versions = v
}

func Versions() []Version {
	return versions
}
