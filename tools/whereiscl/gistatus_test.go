// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/base64"
	"net/http"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestGetGIStatus_passed(t *testing.T) {
	transport := mockTransport{}
	transport.addResponse(
		"https://fuchsia.googlesource.com/integration/+/refs/heads/master/stem?format=TEXT",
		base64.StdEncoding.EncodeToString([]byte(`
<manifest>
  <projects>
    <project name="fuchsia"
             revision="gi_revision"/>
  </projects>
</manifest>
`)),
	)
	// CL's revision is not found in the commits after GI -> PASSED.
	transport.addResponse(
		"https://fuchsia.googlesource.com/fuchsia/+log/gi_revision..HEAD?format=JSON",
		`)]}'
{
  "log": [
    {
      "commit": "abcd"
    },
    {
      "commit": "wxyz"
    }
  ]
}
`,
	)
	http.DefaultClient.Transport = &transport

	ci := changeInfo{
		Project:         "fuchsia",
		CurrentRevision: "cl_revision",
	}
	got, err := getGIStatus(&ci)
	if err != nil {
		t.Fatalf("getGIStatus: %v", err)
	}
	var want giStatus = "PASSED"
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("getGIStatus: mismatch (-want +got):\n%s", d)
	}
}

func TestGetGIStatus_pending(t *testing.T) {
	transport := mockTransport{}
	transport.addResponse(
		"https://fuchsia.googlesource.com/integration/+/refs/heads/master/stem?format=TEXT",
		base64.StdEncoding.EncodeToString([]byte(`
<manifest>
  <projects>
    <project name="fuchsia"
             revision="gi_revision"/>
  </projects>
</manifest>
`)),
	)
	// CL's revision is found in the commits after GI -> PENDING.
	transport.addResponse(
		"https://fuchsia.googlesource.com/fuchsia/+log/gi_revision..HEAD?format=JSON",
		`)]}'
{
  "log": [
    {
      "commit": "abcd"
    },
    {
      "commit": "cl_revision"
    },
    {
      "commit": "wxyz"
    }
  ]
}
`,
	)
	http.DefaultClient.Transport = &transport

	ci := changeInfo{
		Project:         "fuchsia",
		CurrentRevision: "cl_revision",
	}
	got, err := getGIStatus(&ci)
	if err != nil {
		t.Fatalf("getGIStatus: %v", err)
	}
	var want giStatus = "PENDING"
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("getGIStatus: mismatch (-want +got):\n%s", d)
	}
}
