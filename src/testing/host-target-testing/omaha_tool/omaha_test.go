// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package omaha_tool

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"testing"
)

func argsForTest() OmahaToolArgs {
	return OmahaToolArgs{
		ToolPath:       filepath.Join(filepath.Join(filepath.Dir(os.Args[0]), "test_data", "system-tests"), "mock-omaha-server"),
		PrivateKeyId:   "123456789",
		PrivateKeyPath: filepath.Join(filepath.Join(filepath.Dir(os.Args[0]), "test_data", "system-tests"), "test_private_key.pem"),
		AppId:          "some-app-id",
		LocalHostname:  "localhost",
	}
}

func readExpected(t *testing.T, reader io.Reader, expected string) {
	expected_len := 1000
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
			AppId: "some-app-id",
			UpdateCheck: requestUpdateCheck{
				UpdateDisabled: false,
			},
			Cohort:  "1:1:",
			Version: "0.1.2.3",
		}}}})
	if err != nil {
		t.Fatalf("Unable to marshal JSON. %s", err)
	}
	return r
}

func TestSingleRequest(t *testing.T) {
	ctx := context.Background()
	var stdout bytes.Buffer
	var stderr bytes.Buffer

	o, err := NewOmahaServer(ctx, argsForTest(), &stdout, &stderr)
	if err != nil {
		t.Fatalf("failed to create omaha server: %v\nstdout: %s\nstderr: %s\n", err, stdout.String(), stderr.String())
	}
	defer o.Shutdown(ctx)

	if err := o.SetPkgURL(ctx, "fuchsia-pkg://fuchsia.com/update/0?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef"); err != nil {
		t.Fatalf("SetPkgURL should not fail with the given input. %s", err)
	}

	req := getTestOmahaRequest(t)
	resp, err := http.Post(o.URL(), "application/json", bytes.NewBuffer(req))
	if err != nil {
		t.Fatalf("Get request shouldn't fail. err: %s\nstdout: %s\nstderr: %s\n", err, stdout.String(), stderr.String())
	}

	dec := json.NewDecoder(resp.Body)
	var data response
	if err = dec.Decode(&data); err != nil {
		t.Fatalf("Could not decode data: %s", resp.Body)
	}

	app := data.Response.App[0]

	if app.AppId != "some-app-id" {
		t.Fatalf("AppId should be 'some-app-id', is %s", app.AppId)
	}

	updateCheck := app.UpdateCheck

	if updateCheck.Status != "ok" {
		t.Fatalf("Status should be 'ok', is %s", updateCheck.Status)
	}

	if updateCheck.Urls.Url[0].Codebase != "fuchsia-pkg://fuchsia.com/" {
		t.Fatalf(
			"Update package host should be 'fuchsia-pkg://fuchsia.com/', is %s",
			updateCheck.Urls.Url[0].Codebase)
	}

	action := updateCheck.Manifest.Actions.Action[0]

	if action.Run != "update/0?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" {
		t.Fatalf(
			"Manifest action should have 'update/0?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef', is %s",
			action.Run)
	}

	if action.Event != "install" {
		t.Fatalf(
			"First manifest action should have event 'install', is %s",
			action.Event)
	}

	action = data.Response.App[0].UpdateCheck.Manifest.Actions.Action[1]

	if action.Event != "postinstall" {
		t.Fatalf(
			"Second manifest action should have event 'postinstall', is %s",
			action.Event)
	}

	if updateCheck.Manifest.Packages.Pkg[0].Name != "update/0?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" {
		t.Fatalf(
			"Manifest package should have 'update/0?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef', is %s",
			updateCheck.Manifest.Packages.Pkg[0].Name)
	}

	if err := o.Shutdown(ctx); err != nil {
		t.Fatalf("shutdown should not have failed: %s", err)
	}

	req = getTestOmahaRequest(t)
	resp, err = http.Get(o.URL())
	if err == nil {
		t.Fatalf("Server was shutdown, get request should have failed.")
	}
}

func QueryWithTestOmahaRequest(t *testing.T, o *OmahaTool, stdout bytes.Buffer, stderr bytes.Buffer, expectedCodeBaseValue string, expectedRunValue string) {
	req := getTestOmahaRequest(t)
	resp, err := http.Post(o.URL(), "application/json", bytes.NewBuffer(req))
	if err != nil {
		t.Fatalf("Get request shouldn't fail. err: %s\nstdout: %s\nstderr: %s\n", err, stdout.String(), stderr.String())
	}

	dec := json.NewDecoder(resp.Body)
	var data response
	if err = dec.Decode(&data); err != nil {
		t.Fatalf("Could not decode")
	}

	codebase := data.Response.App[0].UpdateCheck.Urls.Url[0].Codebase
	if codebase != expectedCodeBaseValue {
		t.Fatalf("URL codebase should have '%s', is %s", expectedCodeBaseValue, codebase)
	}

	action := data.Response.App[0].UpdateCheck.Manifest.Actions.Action[0]
	if action.Run != expectedRunValue {
		t.Fatalf("Manifest action should have '%s', is %s", expectedRunValue, action.Run)
	}
}

func TestSetPkgUrlOnServer(t *testing.T) {
	ctx := context.Background()
	var stdout bytes.Buffer
	var stderr bytes.Buffer

	o, err := NewOmahaServer(ctx, argsForTest(), &stdout, &stderr)
	if err != nil {
		t.Fatalf("failed to create omaha server: %v\nstdout: %s\nstderr: %s\n", err, stdout.String(), stderr.String())
	}
	defer o.Shutdown(ctx)

	if err := o.SetPkgURL(ctx, "bad"); err == nil {
		t.Fatalf("SetPkgURL should fail when given string 'bad'.")
	}

	if err := o.SetPkgURL(ctx, "fuchsia-pkg://fuchsia.com/update/0?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef"); err != nil {
		t.Fatalf("SetPkgURL should not fail with the given input. %s", err)
	}

	QueryWithTestOmahaRequest(t, o, stdout, stderr, "fuchsia-pkg://fuchsia.com/", "update/0?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef")

	if err := o.SetPkgURL(ctx, "fuchsia-pkg://other-domain.com/foo/1?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead"); err != nil {
		t.Fatalf("Setting Pkg URL on a running server shouldn't fail. err: %s\nstdout: %s\n stderr: %s\n", err, stdout.String(), stderr.String())
	}

	QueryWithTestOmahaRequest(t, o, stdout, stderr, "fuchsia-pkg://other-domain.com/", "foo/1?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead")
}

func TestSingleRequestBeforeMerkleSet(t *testing.T) {
	ctx := context.Background()
	var stdout bytes.Buffer
	var stderr bytes.Buffer

	args := argsForTest()

	o, err := NewOmahaServer(ctx, args, &stdout, &stderr)
	if err != nil {
		t.Fatalf("failed to create omaha server: %v\nstdout: %s\nstderr: %s\n", err, stdout.String(), stderr.String())
	}
	defer o.Shutdown(ctx)

	resp, err := http.Post(o.URL(), "application/json", bytes.NewBuffer(getTestOmahaRequest(t)))
	if err != nil {
		t.Fatalf("Get request ought to fail. err: %s\nstdout: %s\nstderr: %s\n", err, stdout.String(), stderr.String())
	}
	if resp.StatusCode != 500 {
		t.Fatalf("Querying mock-omaha-server before |responses_by_appid| is configured ought to return a 500.")
	}

	if err := o.SetPkgURL(ctx, "fuchsia-pkg://other-domain.com/foo/1?hash=beefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdead"); err != nil {
		t.Fatalf("Setting Pkg URL on a running server shouldn't fail. err: %s\nstdout: %s\n stderr: %s\n", err, stdout.String(), stderr.String())
	}

	_, err = http.Post(o.URL(), "application/json", bytes.NewBuffer(getTestOmahaRequest(t)))
	if err != nil {
		t.Fatalf("Get request shouldn't fail. err: %s\nstdout: %s\nstderr: %s\n", err, stdout.String(), stderr.String())
	}
}
