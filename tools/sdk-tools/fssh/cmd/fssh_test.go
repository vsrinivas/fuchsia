// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"errors"
	"fmt"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"
)

type testSDKProperties struct {
	err                   error
	expectedTargetAddress string
}

func (testSDK testSDKProperties) RunSSHShell(targetAddress string, sshConfig string, privateKey string, verbose bool, sshArgs []string) error {
	if targetAddress == "" {
		return errors.New("target address must be specified")
	}
	if targetAddress != testSDK.expectedTargetAddress {
		return fmt.Errorf("target address %v did not match expected %v", targetAddress, testSDK.expectedTargetAddress)
	}
	return testSDK.err
}

const defaultIPAddress = "e80::c00f:f0f0:eeee:cccc"

func TestSSH(t *testing.T) {

	defaultDeviceProperties := make(map[string]string)
	defaultDeviceProperties[fmt.Sprintf("test-device.%v", sdkcommon.DeviceIPKey)] = defaultIPAddress
	tests := []struct {
		sdk           sdkProvider
		verbose       bool
		targetAddress string
		sshConfig     string
		privateKey    string
		args          []string
		expectedError string
	}{
		{
			sdk:           testSDKProperties{},
			expectedError: "target address must be specified",
		},
		{
			sdk:           testSDKProperties{expectedTargetAddress: defaultIPAddress},
			targetAddress: defaultIPAddress,
			expectedError: "",
		},
		{
			sdk: testSDKProperties{
				expectedTargetAddress: defaultIPAddress,
			},
			targetAddress: defaultIPAddress,
			expectedError: "",
		},
		{
			sdk: testSDKProperties{
				expectedTargetAddress: defaultIPAddress,
			},
			targetAddress: defaultIPAddress,
			verbose:       true,
			expectedError: "",
		},
		{
			sdk: testSDKProperties{
				expectedTargetAddress: defaultIPAddress,
			},
			targetAddress: defaultIPAddress,
			sshConfig:     "custom-config",
			privateKey:    "private-key",
			expectedError: "",
		},
	}

	for _, test := range tests {
		err := ssh(test.sdk, test.verbose, test.targetAddress, test.sshConfig, test.privateKey, test.args)
		if err != nil {
			message := fmt.Sprintf("%v", err)
			if message != test.expectedError {
				t.Fatalf("Unexpected error %v does not match %v", message, test.expectedError)
			}
		} else if test.expectedError != "" {
			t.Fatalf("Expected error '%v', but got no error", test.expectedError)
		}
	}
}
