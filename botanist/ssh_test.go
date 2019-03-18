// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"io/ioutil"
	"net"
	"os"
	"testing"
)

func TestNetwork(t *testing.T) {
	tests := []struct {
		id      int
		addr    net.Addr
		family  string
		wantErr bool
	}{
		// Valid tcp addresses.
		{1, &net.TCPAddr{IP: net.IPv4(1, 2, 3, 4)}, "tcp", false},
		{2, &net.UDPAddr{IP: net.IPv4(5, 6, 7, 8)}, "tcp", false},
		{3, &net.IPAddr{IP: net.IPv4(9, 10, 11, 12)}, "tcp", false},

		// Valid tcp6 addresses.
		{4, &net.TCPAddr{IP: net.IPv6loopback}, "tcp6", false},
		{5, &net.UDPAddr{IP: net.ParseIP("2001:db8::1")}, "tcp6", false},
		{6, &net.IPAddr{IP: net.IPv6linklocalallrouters}, "tcp6", false},

		// Invalid IP addresses
		{7, &net.TCPAddr{IP: net.IP("")}, "", true},
		{8, &net.UDPAddr{IP: net.IP("123456")}, "", true},
		{9, &net.IPAddr{IP: nil}, "", true},

		// Invalid net.AddrType
		{10, &net.UnixAddr{}, "", true},
	}

	for _, test := range tests {
		n, err := network(test.addr)
		if test.wantErr && err == nil {
			t.Errorf("Test %d: got no error; want error", test.id)
		} else if !test.wantErr && err != nil {
			t.Errorf("Test %d: got error %q; want no error", test.id, err)
		} else if n != test.family {
			t.Errorf("Test %d: got %q; want %q", test.id, n, test.family)
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

	validKey1, err := GeneratePrivateKey()
	if err != nil {
		t.Fatalf("Failed to generate private key: %s", err)
	}
	validKey2, err := GeneratePrivateKey()
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
