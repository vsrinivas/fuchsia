// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package omaha

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"net/http"
	"testing"
)

func readExpected(t *testing.T, reader io.Reader, expected string) {
	expected_len := len([]byte(expected))
	b := make([]byte, expected_len)
	n, err := reader.Read(b)
	if err != nil && err != io.EOF {
		t.Fatalf("Unable to read data. %s", err)
	}
	actual := string(b)
	if n != expected_len || actual != expected {
		t.Fatalf("Expected %s, got %s", expected, actual)
	}
}

func getTestOmahaRequest(t *testing.T) []byte {
	r, err := json.Marshal(&request{Request: requestConfig{
		Protocol: "3.0",
		App: []requestApp{{
			AppId: "unit-test-app-id",
		}}}})
	if err != nil {
		t.Fatalf("Unable to marshal JSON. %s", err)
	}
	return r
}

func TestSetPkgURL(t *testing.T) {
	ctx := context.Background()
	o, err := NewOmahaServer(ctx, ":0", "localhost")
	if err != nil {
		t.Fatalf("failed to create omaha server: %v", err)
	}

	if err = o.SetUpdatePkgURL(ctx, "bad"); err == nil {
		t.Fatalf("SetUpdatePkgURL should fail when given string 'bad'.")
	}

	err = o.SetUpdatePkgURL(ctx, "fuchsia-pkg://fuchsia.com/update/0?hash=abcdef")
	if err != nil {
		t.Fatalf("SetUpdatePkgURL should not fail with the given input. %s", err)
	}

	if o.updateHost != "fuchsia-pkg://fuchsia.com" {
		t.Fatalf("updateHost does not match. Got %s expect %s", o.updateHost, "fuchsia-pkg://fuchsia.com")
	}

	if o.updatePkg != "/update/0?hash=abcdef" {
		t.Fatalf("updatePkg does not match. Got %s expect %s", o.updatePkg, "/update/0?hash=abcdef")
	}
}

func TestBeforeAfterSetPkgURL(t *testing.T) {
	ctx := context.Background()
	o, err := NewOmahaServer(ctx, ":0", "localhost")
	if err != nil {
		t.Fatalf("failed to create omaha server: %v", err)
	}

	req := getTestOmahaRequest(t)
	resp, err := http.Post(o.URL(), "application/json", bytes.NewBuffer(req))
	if err != nil {
		t.Fatalf("Get request shouldn't fail. %s", err)
	}

	if resp.StatusCode != 200 {
		t.Fatalf("Get response code not 200 (OK), got %d", resp.StatusCode)
	}
	readExpected(t, resp.Body, `{"status": "noupdate"}`)

	err = o.SetUpdatePkgURL(ctx, "fuchsia-pkg://fuchsia.com/update/0?hash=abcdef")
	if err != nil {
		t.Fatalf("SetUpdatePkgURL should not fail with the given input. %s", err)
	}

	resp, err = http.Post(o.URL(), "application/json", bytes.NewBuffer(req))
	if err != nil {
		t.Fatalf("Get request shouldn't fail. %s", err)
	}

	dec := json.NewDecoder(resp.Body)
	var data response
	if err = dec.Decode(&data); err != nil {
		t.Fatalf("Could not decode")
	}

	app := data.Response.App[0]

	if app.AppId != "unit-test-app-id" {
		t.Fatalf("AppId should be 'unit-test-app-id', is %s", app.AppId)
	}

	updateCheck := app.UpdateCheck

	if updateCheck.Status != "ok" {
		t.Fatalf("Status should be 'ok', is %s", updateCheck.Status)
	}

	if updateCheck.Urls.Url[0].Codebase != "fuchsia-pkg://fuchsia.com" {
		t.Fatalf(
			"Update package host should be 'fuchsia-pkg://fuchsia.com', is %s",
			updateCheck.Urls.Url[0].Codebase)
	}

	action := updateCheck.Manifest.Actions.Action[0]

	if action.Run != "/update/0?hash=abcdef" {
		t.Fatalf(
			"Manifest action should have '/update/0?hash=abcdef', is %s",
			action.Run)
	}

	if action.Event != "update" {
		t.Fatalf(
			"First manifest action should have event 'update', is %s",
			action.Event)
	}

	action = data.Response.App[0].UpdateCheck.Manifest.Actions.Action[1]

	if action.Event != "postinstall" {
		t.Fatalf(
			"Second manifest action should have event 'postinstall', is %s",
			action.Event)
	}

	if updateCheck.Manifest.Packages.Pkg[0].Name != "/update/0?hash=abcdef" {
		t.Fatalf(
			"Manifest package should have '/update?hash=abcdef', is %s",
			updateCheck.Manifest.Packages.Pkg[0].Name)
	}
}

func TestShutdown(t *testing.T) {
	ctx := context.Background()
	o, err := NewOmahaServer(ctx, ":0", "localhost")
	if err != nil {
		t.Fatalf("failed to create omaha server: %v", err)
	}

	req := getTestOmahaRequest(t)
	resp, err := http.Post(o.URL(), "application/json", bytes.NewBuffer(req))
	if err != nil {
		t.Fatalf("Get request shouldn't fail. %s", err)
	}

	if resp.StatusCode != 200 {
		t.Fatalf("Get response code not 200 (OK), got %d", resp.StatusCode)
	}
	readExpected(t, resp.Body, `{"status": "noupdate"}`)

	o.Shutdown(ctx)

	resp, err = http.Get(o.URL())
	if err == nil {
		t.Fatalf("Server was shutdown, get request should have failed.")
	}

}
