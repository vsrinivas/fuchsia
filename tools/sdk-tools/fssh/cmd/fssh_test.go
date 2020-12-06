// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"fmt"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"
)

type testSDKProperties struct {
	properties            map[string]string
	err                   error
	ipAddress             string
	deviceName            string
	expectedTargetAddress string
}

func (testSDK testSDKProperties) GetDefaultDeviceName() (string, error) {
	if testSDK.err != nil {
		return "", testSDK.err
	}
	return testSDK.deviceName, nil
}

func (testSDK testSDKProperties) GetFuchsiaProperty(deviceName string, property string) (string, error) {
	if testSDK.err != nil {
		return "", testSDK.err
	}
	var key = property
	if deviceName != "" {
		key = fmt.Sprintf("%s.%s", deviceName, property)
	}
	return testSDK.properties[key], nil
}
func (testSDK testSDKProperties) GetAddressByName(deviceName string) (string, error) {
	if testSDK.err != nil {
		return "", testSDK.err
	}
	return testSDK.ipAddress, nil
}
func (testSDK testSDKProperties) RunSSHShell(targetAddress string, sshConfig string, privateKey string, verbose bool, sshArgs []string) error {
	if targetAddress != testSDK.expectedTargetAddress {
		return fmt.Errorf("target address %v did not match expected %v", targetAddress, testSDK.expectedTargetAddress)
	}
	return testSDK.err
}

const defaultIPAddress = "e80::c00f:f0f0:eeee:cccc"
const anotherIPAddress = "e80::f00f:c0c0:cccc:1010"

func TestSSH(t *testing.T) {

	defaultDeviceProperties := make(map[string]string)
	defaultDeviceProperties[fmt.Sprintf("test-device.%v", sdkcommon.DeviceIPKey)] = defaultIPAddress
	defaultDeviceProperties[fmt.Sprintf("another-device.%v", sdkcommon.DeviceIPKey)] = anotherIPAddress
	tests := []struct {
		sdk           sdkProvider
		verbose       bool
		deviceName    string
		deviceIP      string
		sshConfig     string
		privateKey    string
		args          []string
		expectedError string
	}{
		{
			sdk:           testSDKProperties{},
			expectedError: "invalid arguments. Need to specify --device-ip or --device-name or use fconfig to configure a default device",
		},
		{
			sdk:           testSDKProperties{deviceName: "test-device"},
			expectedError: "could not get target device IP address for test-device",
		},
		{
			sdk: testSDKProperties{deviceName: "test-device",
				properties:            defaultDeviceProperties,
				expectedTargetAddress: defaultIPAddress,
			},
			expectedError: "",
		},
		{
			sdk: testSDKProperties{deviceName: "test-device",
				ipAddress:             defaultIPAddress,
				expectedTargetAddress: defaultIPAddress,
			},
			expectedError: "",
		},
		{
			sdk:           testSDKProperties{deviceName: "another-device"},
			deviceName:    "test-device",
			expectedError: "could not get target device IP address for test-device",
		},
		{
			sdk: testSDKProperties{
				expectedTargetAddress: defaultIPAddress,
			},
			deviceIP:      defaultIPAddress,
			expectedError: "",
		},
		{
			sdk: testSDKProperties{
				expectedTargetAddress: defaultIPAddress,
				properties:            defaultDeviceProperties,
				deviceName:            "another-device",
			},
			deviceIP:      defaultIPAddress,
			expectedError: "",
		},
		{
			sdk: testSDKProperties{
				expectedTargetAddress: defaultIPAddress,
				properties:            defaultDeviceProperties,
				deviceName:            "another-device",
			},
			deviceIP:      defaultIPAddress,
			deviceName:    "another-device",
			expectedError: "",
		},
		{
			sdk: testSDKProperties{
				expectedTargetAddress: defaultIPAddress,
				properties:            defaultDeviceProperties,
				deviceName:            "target-device",
			},
			deviceIP:      defaultIPAddress,
			deviceName:    "another-device",
			expectedError: "",
		},
	}

	for _, test := range tests {
		err := ssh(test.sdk, test.verbose, test.deviceName, test.deviceIP, test.sshConfig, test.privateKey, test.args)
		if err != nil {
			message := fmt.Sprintf("%v", err)
			if message != test.expectedError {
				t.Fatalf("Unexpected error %v does not match %v", message, test.expectedError)
			}
		} else if test.expectedError != "" {
			t.Fatal("Expected error, but got no error")
		}
	}
}
