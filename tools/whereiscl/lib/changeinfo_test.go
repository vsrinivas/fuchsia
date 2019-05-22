// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"net/http"
	"testing"

	th "fuchsia.googlesource.com/fuchsia/tools/whereiscl/testhelper"
	"github.com/google/go-cmp/cmp"
)

func TestGetChangeInfo(t *testing.T) {
	transport := th.MockTransport{}
	transport.AddResponse(
		"https://fuchsia-review.googlesource.com/changes/?q=987654321&o=CURRENT_REVISION",
		`)]}'
[
  {
    "foo": 42,
    "status": "MERGED",
    "current_revision": "abcdefg"
  }
]`,
	)

	http.DefaultClient.Transport = &transport
	got, err := GetChangeInfo(QueryInfo{
		APIEndpoint: "https://fuchsia-review.googlesource.com",
		CL:          "987654321",
	})
	if err != nil {
		t.Fatalf("GetChangeInfo: %v", err)
	}
	want := &ChangeInfo{
		Status:          CLStatusMerged,
		CurrentRevision: "abcdefg",
	}
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("GetChangeInfo: mismatch (-want +got):\n%s", d)
	}
}

func TestGetChangeInfo_clNotFound(t *testing.T) {
	transport := th.MockTransport{}
	transport.AddResponse(
		"https://fuchsia-review.googlesource.com/changes/?q=987654321&o=CURRENT_REVISION",
		`)]}'
[]`,
	)
	http.DefaultClient.Transport = &transport
	_, err := GetChangeInfo(QueryInfo{
		APIEndpoint: "https://fuchsia-review.googlesource.com",
		CL:          "987654321",
	})
	if err == nil {
		t.Error("GetChangeInfo: error expected; got nil")
	}
}

func TestGetChangeInfo_tooManyCLs(t *testing.T) {
	transport := th.MockTransport{}
	transport.AddResponse(
		"https://fuchsia-review.googlesource.com/changes/?q=987654321&o=CURRENT_REVISION",
		`)]}'
[
  {
    "status": "ACTIVE",
    "current_revision": "abcdefg"
  },
  {
    "status": "MERGED",
    "current_revision": "hijklmn"
  }
]`,
	)
	http.DefaultClient.Transport = &transport
	_, err := GetChangeInfo(QueryInfo{
		APIEndpoint: "https://fuchsia-review.googlesource.com",
		CL:          "987654321",
	})
	if err == nil {
		t.Error("GetChangeInfo: error expected; got nil")
	}
}
