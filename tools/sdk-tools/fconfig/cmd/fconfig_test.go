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

// testSDKProperties is used to inject a current device config to use when returning info
// and an expected config to test setting.
type testSDKProperties struct {
	expectedConfig    sdkcommon.DeviceConfig
	currentConfigs    []sdkcommon.DeviceConfig
	defaultDeviceName string
}

func (sdk testSDKProperties) GetDeviceConfiguration(name string) (sdkcommon.DeviceConfig, error) {
	for _, config := range sdk.currentConfigs {
		if config.DeviceName == name {
			return config, nil
		}
	}
	return sdkcommon.DeviceConfig{}, nil
}

func (sdk testSDKProperties) GetDefaultDeviceName() (string, error) {
	if sdk.defaultDeviceName != "error" {
		return sdk.defaultDeviceName, nil
	}
	return "", errors.New("Test error returned by mock")
}
func (sdk testSDKProperties) GetFuchsiaProperty(deviceName string, property string) (string, error) {

	device := deviceName
	if device == "" {
		device = sdk.defaultDeviceName
	}
	config, err := sdk.GetDeviceConfiguration(device)
	if err != nil {
		return "", err
	}

	switch property {
	case sdkcommon.BucketKey:
		return config.Bucket, nil
	case sdkcommon.DeviceIPKey:
		return config.DeviceIP, nil
	case sdkcommon.DeviceNameKey:
		return config.DeviceName, nil
	case sdkcommon.ImageKey:
		return config.Image, nil
	case sdkcommon.PackagePortKey:
		return config.PackagePort, nil
	case sdkcommon.PackageRepoKey:
		return config.PackageRepo, nil
	case sdkcommon.SSHPortKey:
		return config.SSHPort, nil
	}
	return "", nil
}

func (sdk testSDKProperties) IsValidProperty(property string) bool {
	prodSDK := sdkcommon.SDKProperties{}
	return prodSDK.IsValidProperty(property)
}

func (sdk testSDKProperties) GetDeviceConfigurations() ([]sdkcommon.DeviceConfig, error) {
	return sdk.currentConfigs, nil
}

func (sdk testSDKProperties) SaveDeviceConfiguration(newConfig sdkcommon.DeviceConfig) error {

	index := -1
	for i, config := range sdk.currentConfigs {
		if config.DeviceName == newConfig.DeviceName {
			index = i
			break
		}
	}
	if index >= 0 {
		sdk.currentConfigs[index] = newConfig
	} else {
		sdk.currentConfigs = append(sdk.currentConfigs, newConfig)
	}

	if newConfig.DeviceIP != sdk.expectedConfig.DeviceIP {
		return fmt.Errorf("Unexpected DeviceIP %v expected %v", newConfig.DeviceIP, sdk.expectedConfig.DeviceIP)
	}
	if newConfig.Bucket != sdk.expectedConfig.Bucket {
		return fmt.Errorf("Unexpected Bucket %v expected %v", newConfig.Bucket, sdk.expectedConfig.Bucket)
	}
	if newConfig.DeviceName != sdk.expectedConfig.DeviceName {
		return fmt.Errorf("Unexpected DeviceName %v expected %v", newConfig.DeviceName, sdk.expectedConfig.DeviceName)
	}
	if newConfig.Image != sdk.expectedConfig.Image {
		return fmt.Errorf("Unexpected Image %v expected %v", newConfig.Image, sdk.expectedConfig.Image)
	}
	if newConfig.IsDefault != sdk.expectedConfig.IsDefault {
		return fmt.Errorf("Unexpected IsDefault %v expected %v", newConfig.IsDefault, sdk.expectedConfig.IsDefault)
	}
	if newConfig.PackagePort != sdk.expectedConfig.PackagePort {
		return fmt.Errorf("Unexpected PackagePort %v expected %v", newConfig.PackagePort, sdk.expectedConfig.PackagePort)
	}
	if newConfig.PackageRepo != sdk.expectedConfig.PackageRepo {
		return fmt.Errorf("Unexpected PackageRepo %v expected %v", newConfig.PackageRepo, sdk.expectedConfig.PackageRepo)
	}
	if newConfig.SSHPort != sdk.expectedConfig.SSHPort {
		return fmt.Errorf("Unexpected SSHPort %v expected %v", newConfig.SSHPort, sdk.expectedConfig.SSHPort)
	}
	return nil
}

func TestSetDevice(t *testing.T) {
	testSDK := testSDKProperties{}

	// Test empty
	propertyMap := make(map[string]string)
	err := doSetDevice(testSDK, "new-device-name", propertyMap)
	if err == nil {
		t.Fatal("Expected error for empty properties, but got none")
	}

	// Test just changing the default flag.
	propertyMap = make(map[string]string)
	propertyMap[sdkcommon.DefaultKey] = "True"
	testSDK.defaultDeviceName = "other-default-device"
	testSDK.currentConfigs = []sdkcommon.DeviceConfig{
		{
			DeviceName: "new-device-name",
			IsDefault:  false,
		},
	}
	testSDK.expectedConfig.IsDefault = true
	testSDK.expectedConfig.DeviceName = "new-device-name"

	err = doSetDevice(testSDK, "new-device-name", propertyMap)
	if err != nil {
		t.Fatalf("Unexpected error: %v", err)
	}

	testSDK.currentConfigs[0].DeviceIP = "10.10.10.10"
	testSDK.currentConfigs[0].SSHPort = "2000"

	testSDK.expectedConfig.DeviceIP = "10.0.0.1"
	testSDK.expectedConfig.SSHPort = testSDK.currentConfigs[0].SSHPort

	propertyMap[sdkcommon.DeviceIPKey] = testSDK.expectedConfig.DeviceIP
	err = doSetDevice(testSDK, "new-device-name", propertyMap)
	if err != nil {
		t.Fatalf("Unexpected error: %v", err)
	}

}

func TestGet(t *testing.T) {
	testSDK := testSDKProperties{}

	if _, err := doGet(testSDK, sdkcommon.DeviceNameKey); err != nil {
		t.Fatal(err)
	}

	if _, err := doGet(testSDK, "unknown-prop"); err != nil {
		expected := "Invalid property name: unknown-prop"
		actual := fmt.Sprintf("%v", err)
		if expected != actual {
			t.Fatalf("Expected error [%v], got [%v]", expected, actual)
		}
	}

	// testing that the device name matters
	testSDK.defaultDeviceName = "new-device-name"
	testSDK.currentConfigs = []sdkcommon.DeviceConfig{
		{
			Bucket:     "test-bucket",
			DeviceIP:   "10.10.10.10",
			DeviceName: "new-device-name",
			IsDefault:  false,
			SSHPort:    "8022",
		},
	}
	value, err := doGet(testSDK, sdkcommon.BucketKey)
	if err != nil {
		t.Fatal(err)
	}
	if value != "test-bucket" {
		t.Fatalf("Expected [%v] , got [%v]", value, "test-bucket")
	}

	value, err = doGet(testSDK, "unknown-device."+sdkcommon.BucketKey)
	if err != nil {
		t.Fatal(err)
	}
	if value != "" {
		t.Fatalf("Expected [%v] , got [%v]", value, "")
	}

	value, err = doGet(testSDK, "new-device-name."+sdkcommon.BucketKey)
	if err != nil {
		t.Fatal(err)
	}
	if value != "test-bucket" {
		t.Fatalf("Expected [%v] , got [%v]", value, "test-bucket")
	}
}

func TestList(t *testing.T) {
	testSDK := testSDKProperties{}

	if err := doList(testSDK); err != nil {
		t.Fatal(err)
	}
}

func TestGetAll(t *testing.T) {
	output := []string{}
	stdoutPrintln = func(value ...interface{}) (int, error) {
		line := fmt.Sprintln(value...)
		output = append(output, line)
		return len(line), nil
	}
	defer func() {
		stdoutPrintln = fmt.Println
	}()
	tests := []struct {
		sdk        SDKProvider
		deviceName string
		expected   []string
	}{
		{
			sdk:        testSDKProperties{},
			deviceName: "",
			expected: []string{
				`{
"device-name": "",
"bucket": "",
"image": "",
"device-ip": "",
"ssh-port": "",
"package-repo": "",
"package-port": "",
"default": false
}
`,
			},
		},
		{
			sdk: testSDKProperties{currentConfigs: []sdkcommon.DeviceConfig{
				{
					DeviceName: "device1",
				},
			}},
			deviceName: "",
			expected: []string{
				`{
"device-name": "",
"bucket": "",
"image": "",
"device-ip": "",
"ssh-port": "",
"package-repo": "",
"package-port": "",
"default": false
}
`,
			},
		},
		{
			sdk: testSDKProperties{currentConfigs: []sdkcommon.DeviceConfig{
				{
					DeviceName: "device1",
				},
			},
				defaultDeviceName: "device1",
			},
			deviceName: "device1",
			expected: []string{
				`{
"device-name": "device1",
"bucket": "",
"image": "",
"device-ip": "",
"ssh-port": "",
"package-repo": "",
"package-port": "",
"default": false
}
`,
			},
		},
	}
	for _, test := range tests {
		output = []string{}
		handleGetAll(test.sdk, test.deviceName)
		if len(output) != len(test.expected) {
			t.Fatalf("Output length mismatch %v does not match expected %v", output, test.expected)
		}
		for i, line := range output {
			if line != test.expected[i] {
				t.Fatalf("Output %q does not match expected %q", output, test.expected)
			}
		}
	}
}
