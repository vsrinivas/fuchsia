package main

import (
	"encoding/base64"
	"net/http"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestGetGIStatus(t *testing.T) {
	body := base64.StdEncoding.EncodeToString([]byte(`
<manifest>
  <projects>
    <project name="fuchsia"
             revision="gi_revision"/>
  </projects>
</manifest>
`))
	http.DefaultClient.Transport = &mockTransport{body: body}

	ci := changeInfo{Project: "fuchsia"}
	got, err := getGIStatus(&ci)
	if err != nil {
		t.Fatalf("getGIStatus: %v", err)
	}
	// TODO: Update this after finishing implementation of giStatus().
	var want giStatus = "PASSED"
	if d := cmp.Diff(want, got); d != "" {
		t.Errorf("getGIStatus: mismatch (-want +got):\n%s", d)
	}
}
