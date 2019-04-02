// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package secrets

import (
	"context"
	"net/http"
	"net/http/httptest"
	"reflect"
	"testing"

	"go.chromium.org/luci/lucictx"
)

func TestGettingSecrets(t *testing.T) {
	secretBytes := []byte(`{"testNameA":"SECRETA","testNameB":"SECRETB"}`)
	swarming := lucictx.Swarming{
		SecretBytes: secretBytes,
	}
	ctx := lucictx.SetSwarming(context.Background(), &swarming)

	expectedSecrets := testSecrets{
		"testNameA": "SECRETA",
		"testNameB": "SECRETB",
	}
	actualSecrets := getSecrets(ctx)
	if actualSecrets == nil {
		t.Fatal("no secrets found")
	}
	if !reflect.DeepEqual(actualSecrets, &expectedSecrets) {
		t.Errorf("Returned secrets \"%v\" do not match the expected: \"%v\"\n",
			*actualSecrets, expectedSecrets)
	}
}

func TestServingSecrets(t *testing.T) {
	// Returns an GET request for the secret associated to |testName|.
	secretRequest := func(testName string) *http.Request {
		request, err := http.NewRequest(http.MethodGet, "/"+testName, nil)
		if err != nil {
			t.Fatal(err)
		}
		return request
	}

	secrets := testSecrets{
		"foo_unittests": "FOO-SECRET",
		"bar_e2e_tests": "BAR-SECRET",
	}
	handler := http.HandlerFunc(secrets.serveSecret)

	// Checks that the expected content and code were returned in a mock HTTP response.
	expectValidResponse := func(t *testing.T, testName string, expectedCode int,
		expectedContent string) {
		recorder := httptest.NewRecorder()
		handler.ServeHTTP(recorder, secretRequest(testName))
		if actualCode := recorder.Code; actualCode != expectedCode {
			t.Errorf("serveSecret() response code: %v\n; %v was expected for test \"%v\"\n",
				actualCode, expectedCode, testName)
		}
		if actualContent := recorder.Body.String(); actualContent != expectedContent {
			t.Errorf("serveSecret() failed to returned \"%v\" instead of \"%v\" for test \"%v\"\n",
				actualContent, expectedContent, testName)
		}
	}

	t.Run("Succeeds when associated secret exists", func(t *testing.T) {
		expectValidResponse(t, "foo_unittests", http.StatusOK, "FOO-SECRET")
	})

	t.Run("Fails when associated secret does not exist", func(t *testing.T) {
		expectValidResponse(t, "non_existant_unittests", http.StatusNotFound, "")
	})
}
