// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"bytes"
	"encoding/json"
	"io/ioutil"
	"reflect"
	"testing"
)

func TestCompiledVersionMatchesBuildVersion(t *testing.T) {
	expectedBytes, err := ioutil.ReadFile("host_x64/test_data/version-history/go/version_history.json")
	if err != nil {
		t.Fatal(err)
	}

	if !bytes.Equal(expectedBytes, versionHistoryBytes) {
		t.Fatalf("expected:\n%s\ngot:\n%s", expectedBytes, versionHistoryBytes)
	}
}

func TestParseHistoryWorks(t *testing.T) {
	b, err := json.Marshal(versionHistory{
		SchemaId: versionHistorySchemaId,
		Data: versionHistoryData{
			Name: versionHistoryName,
			Type: versionHistoryType,
			Versions: []versionHistoryVersion{
				{APILevel: "1", ABIRevision: "1"},
				{APILevel: "2", ABIRevision: "0x2"},
			},
		},
	})
	if err != nil {
		t.Fatal(err)
	}

	actual, err := parseVersionHistory(b)
	if err != nil {
		t.Fatal(err)
	}

	expected := []Version{
		{APILevel: 1, ABIRevision: 1},
		{APILevel: 2, ABIRevision: 2},
	}

	if !reflect.DeepEqual(expected, actual) {
		t.Fatalf("expected %+v, got %+v", expected, actual)
	}
}

func TestParseHistoryRejectsInvalidSchema(t *testing.T) {
	b, err := json.Marshal(&versionHistory{
		SchemaId: "some-schema",
		Data: versionHistoryData{
			Name:     versionHistoryName,
			Type:     versionHistoryType,
			Versions: []versionHistoryVersion{},
		},
	})
	if err != nil {
		t.Fatal(err)
	}

	vs, err := parseVersionHistory(b)
	if err == nil {
		t.Fatalf("expected error, got: %+v", vs)
	}
}

func TestParseHistoryRejectsInvalidName(t *testing.T) {
	b, err := json.Marshal(&versionHistory{
		SchemaId: versionHistorySchemaId,
		Data: versionHistoryData{
			Name:     "some-name",
			Type:     versionHistoryType,
			Versions: []versionHistoryVersion{},
		},
	})
	if err != nil {
		t.Fatal(err)
	}

	vs, err := parseVersionHistory(b)
	if err == nil {
		t.Fatalf("expected error, got: %+v", vs)
	}
}

func TestParseHistoryRejectsInvalidType(t *testing.T) {
	b, err := json.Marshal(&versionHistory{
		SchemaId: versionHistorySchemaId,
		Data: versionHistoryData{
			Name:     versionHistoryName,
			Type:     "some-type",
			Versions: []versionHistoryVersion{},
		},
	})
	if err != nil {
		t.Fatal(err)
	}

	vs, err := parseVersionHistory(b)
	if err == nil {
		t.Fatalf("expected error, got: %+v", vs)
	}
}

func TestParseHistoryRejectsInvalidVersions(t *testing.T) {
	for _, testVersion := range []versionHistoryVersion{
		{APILevel: "some-version", ABIRevision: "1"},
		{APILevel: "-1", ABIRevision: "1"},
		{APILevel: "1", ABIRevision: "some-revision"},
		{APILevel: "1", ABIRevision: "-1"},
	} {
		b, err := json.Marshal(&versionHistory{
			SchemaId: versionHistorySchemaId,
			Data: versionHistoryData{
				Name:     versionHistoryName,
				Type:     versionHistoryType,
				Versions: []versionHistoryVersion{testVersion},
			},
		})
		if err != nil {
			t.Fatal(err)
		}

		vs, err := parseVersionHistory(b)
		if err == nil {
			t.Fatalf("expected error, got: %+v", vs)
		}
	}

}
