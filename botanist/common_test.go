// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package botanist

import (
	"io/ioutil"
	"os"
	"testing"

	"fuchsia.googlesource.com/tools/sshutil"
)

func TestLoadDevicePropertiesSlice(t *testing.T) {
	tests := []struct {
		name        string
		jsonStr     string
		expectedLen int
		expectErr   bool
	}{
		// Valid configs.
		{"ValidListConfig", `[{"nodename":"upper-drank-wick-creek"},{"nodename":"siren-swoop-wick-hasty"}]`, 2, false},
		{"ValidSingleConfig", `{"nodename":"upper-drank-wick-creek"}`, 1, false},
		// Invalid configs.
		{"InvalidListConfig", `{{"nodename":"upper-drank-wick-creek"},{"nodename":"siren-swoop-wick-hasty"}}`, 0, true},
		{"InvalidSingleConfig", `{"upper-drank-wick-creek"}`, 0, true},
	}
	for _, test := range tests {
		tmpfile, err := ioutil.TempFile(os.TempDir(), "common_test")
		if err != nil {
			t.Fatalf("Failed to create test device properties file: %s", err)
		}
		defer os.Remove(tmpfile.Name())

		content := []byte(test.jsonStr)
		if _, err := tmpfile.Write(content); err != nil {
			t.Fatalf("Failed to write to test device properties file: %s", err)
		}

		propertiesSlice, err := LoadDeviceProperties(tmpfile.Name())

		if test.expectErr && err == nil {
			t.Errorf("Test%v: Exepected errors; no errors found", test.name)
		}

		if !test.expectErr && err != nil {
			t.Errorf("Test%v: Exepected no errors; found error - %v", test.name, err)
		}

		if len(propertiesSlice) != test.expectedLen {
			t.Errorf("Test%v: Expected %d nodes; found %d", test.name, test.expectedLen, len(propertiesSlice))
		}

		if err := tmpfile.Close(); err != nil {
			t.Fatal(err)
		}
	}
}

func TestSSHSignersFromDeviceProperties(t *testing.T) {
	tests := []struct {
		name        string
		device1Keys []string
		device2Keys []string
		expectedLen int
		expectErr   bool
	}{
		// Valid configs.
		{"ValidSameKeyConfig", []string{"valid1"}, []string{"valid1"}, 1, false},
		{"ValidDiffKeysWithDuplicateConfig", []string{"valid1", "valid2"}, []string{"valid1"}, 2, false},
		{"ValidDiffKeysConfig", []string{"valid1"}, []string{"valid2"}, 2, false},
		{"ValidEmptyKeysConfig", []string{}, []string{}, 0, false},
		// Invalid configs.
		{"InvalidKeyFileConfig", []string{"valid1"}, []string{"invalid"}, 0, true},
		{"MissingKeyFileConfig", []string{"missing"}, []string{}, 0, true},
	}

	validKey1, err := sshutil.GeneratePrivateKey()
	if err != nil {
		t.Fatalf("Failed to generate private key: %s", err)
	}
	validKey2, err := sshutil.GeneratePrivateKey()
	if err != nil {
		t.Fatalf("Failed to generate private key: %s", err)
	}
	invalidKey := []byte("invalidKey")

	keys := []struct {
		name        string
		keyContents []byte
	}{
		{"valid1", validKey1}, {"valid2", validKey2}, {"invalid", invalidKey},
	}

	keyNameToPath := make(map[string]string)
	keyNameToPath["missing"] = "/path/to/nonexistent/key"
	for _, key := range keys {
		tmpfile, err := ioutil.TempFile(os.TempDir(), key.name)
		if err != nil {
			t.Fatalf("Failed to create test device properties file: %s", err)
		}
		defer os.Remove(tmpfile.Name())
		if _, err := tmpfile.Write(key.keyContents); err != nil {
			t.Fatalf("Failed to write to test device properties file: %s", err)
		}
		if err := tmpfile.Close(); err != nil {
			t.Fatal(err)
		}
		keyNameToPath[key.name] = tmpfile.Name()
	}

	for _, test := range tests {
		var keyPaths1 []string
		for _, keyName := range test.device1Keys {
			keyPaths1 = append(keyPaths1, keyNameToPath[keyName])
		}
		var keyPaths2 []string
		for _, keyName := range test.device2Keys {
			keyPaths2 = append(keyPaths2, keyNameToPath[keyName])
		}
		devices := []DeviceProperties{
			DeviceProperties{"device1", &Config{}, keyPaths1},
			DeviceProperties{"device2", &Config{}, keyPaths2},
		}
		signers, err := SSHSignersFromDeviceProperties(devices)
		if test.expectErr && err == nil {
			t.Errorf("Test%v: Expected errors; no errors found", test.name)
		}
		if !test.expectErr && err != nil {
			t.Errorf("Test%v: Expected no errors; found error - %v", test.name, err)
		}
		if len(signers) != test.expectedLen {
			t.Errorf("Test%v: Expected %d signers; found %d", test.name, test.expectedLen, len(signers))
		}
	}
}
