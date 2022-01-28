// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"strings"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"
)

type testSDKProperties struct {
	DataPath string
	device   sdkcommon.DeviceConfig
	err      error
}

func (testSDK testSDKProperties) ResolveTargetAddress(deviceIP string, deviceName string) (sdkcommon.DeviceConfig, error) {
	return testSDK.device, testSDK.err
}

func (testSDK testSDKProperties) GetSDKDataPath() string {
	return testSDK.DataPath
}

const (
	fakeFoundIPAddr  = "fake-found-ip-addr"
	fakeFoundName    = "fake-found-name"
	fakeFoundIPAddr2 = "fake-found-ip-addr2"
	fakeFoundName2   = "fake-found-name-2"
	fakeNotFoundName = "fake-not-found-name"
)

func TestCommandLineParseFlags(t *testing.T) {
	// Note this is testing the flag parsing so even though 22 is protected and
	// 8888 is already in-use, this is a test that we are reading the CLI input
	// correctly so those are still expected to be included at this level.
	expectedPorts := []int{22, 8888, 9058, 9059, 9060}
	flags := []string{
		fmt.Sprintf("--%s", remoteHostFlag),
		"fake-remote_host",
		fmt.Sprintf("--%s=22,8888,9058,9059,9060", tunnelPortsFlag),
		fmt.Sprintf("--%s", deviceIPFlag),
		fakeFoundIPAddr,
	}
	flagSet := flag.NewFlagSet("test-flag-set", flag.PanicOnError)
	cmd := &tunnelCmd{}
	cmd.SetFlags(flagSet)
	flagSet.Parse(flags)
	if len(cmd.tunnelPorts) != len(expectedPorts) {
		t.Fatalf("got length %d, want %d", len(cmd.tunnelPorts), len(expectedPorts))
	}
	for i, cmdPort := range cmd.tunnelPorts {
		if cmdPort != expectedPorts[i] {
			t.Fatalf("index %d got %d, want %d", i, cmdPort, expectedPorts[i])
		}
	}

}

func TestParseFlags(t *testing.T) {
	fakeHomePath := t.TempDir()
	fakeSSHConfigPath, err := sdkcommon.WriteTempFile([]byte("fake-ssh-config-contents"))
	if err != nil {
		t.Fatalf("could not create temporary SSH config file: %s", err)
	}
	defer os.Remove(fakeSSHConfigPath)
	var tests = []struct {
		tunnelCmd              *tunnelCmd
		expectedRemoteHost     string
		expectedDeviceIP       string
		expectedDeviceName     string
		expectedSSHConfig      string
		expectedPrintSSHConfig bool
		sdk                    testSDKProperties
	}{
		{
			tunnelCmd: &tunnelCmd{
				remoteHost:     "fake.remote.host",
				deviceIP:       "",
				deviceName:     fakeFoundName,
				sshConfig:      "",
				printSSHConfig: false,
				tunnelPorts:    intSlice([]int{}),
			},
			expectedRemoteHost:     "fake.remote.host",
			expectedDeviceIP:       fakeFoundIPAddr,
			expectedDeviceName:     fakeFoundName,
			expectedSSHConfig:      "",
			expectedPrintSSHConfig: false,
			sdk: testSDKProperties{
				DataPath: fakeHomePath,
				device: sdkcommon.DeviceConfig{
					DeviceIP:   fakeFoundIPAddr,
					DeviceName: fakeFoundName,
				},
			},
		},
		{
			tunnelCmd: &tunnelCmd{
				remoteHost:     "fake.remote.host",
				deviceIP:       fakeFoundIPAddr,
				deviceName:     "",
				sshConfig:      "",
				printSSHConfig: false,
				tunnelPorts:    intSlice([]int{}),
			},
			expectedRemoteHost:     "fake.remote.host",
			expectedDeviceIP:       fakeFoundIPAddr,
			expectedDeviceName:     "",
			expectedSSHConfig:      "",
			expectedPrintSSHConfig: false,
			sdk: testSDKProperties{
				DataPath: fakeHomePath,
				device: sdkcommon.DeviceConfig{
					DeviceIP: fakeFoundIPAddr,
				},
			},
		},
		{
			tunnelCmd: &tunnelCmd{
				remoteHost:     "fake.remote.host",
				deviceIP:       fakeFoundIPAddr,
				deviceName:     fakeFoundName,
				sshConfig:      "",
				printSSHConfig: false,
				tunnelPorts:    intSlice([]int{}),
			},
			expectedRemoteHost:     "fake.remote.host",
			expectedDeviceIP:       fakeFoundIPAddr,
			expectedDeviceName:     "",
			expectedSSHConfig:      "",
			expectedPrintSSHConfig: false,
			sdk: testSDKProperties{
				DataPath: fakeHomePath,
				device: sdkcommon.DeviceConfig{
					DeviceIP: fakeFoundIPAddr,
				},
			},
		},
		{
			tunnelCmd: &tunnelCmd{
				remoteHost:     "fake.remote.host",
				deviceIP:       fakeFoundIPAddr,
				deviceName:     fakeFoundName,
				sshConfig:      "",
				printSSHConfig: true,
				tunnelPorts:    intSlice([]int{}),
			},
			expectedRemoteHost:     "fake.remote.host",
			expectedDeviceIP:       fakeFoundIPAddr,
			expectedDeviceName:     "",
			expectedSSHConfig:      "",
			expectedPrintSSHConfig: true,
			sdk: testSDKProperties{
				DataPath: fakeHomePath,
				device: sdkcommon.DeviceConfig{
					DeviceIP: fakeFoundIPAddr,
				},
			},
		},
		{
			tunnelCmd: &tunnelCmd{
				remoteHost:     "fake.remote.host",
				deviceIP:       fakeFoundIPAddr,
				deviceName:     fakeFoundName,
				sshConfig:      fakeSSHConfigPath,
				printSSHConfig: true,
				tunnelPorts:    intSlice([]int{}),
			},
			expectedRemoteHost:     "fake.remote.host",
			expectedDeviceIP:       fakeFoundIPAddr,
			expectedDeviceName:     "",
			expectedSSHConfig:      fakeSSHConfigPath,
			expectedPrintSSHConfig: true,
			sdk: testSDKProperties{
				DataPath: fakeHomePath,
				device: sdkcommon.DeviceConfig{
					DeviceIP: fakeFoundIPAddr,
				},
			},
		},
		{
			tunnelCmd: &tunnelCmd{
				remoteHost:     "fake.remote.host",
				deviceIP:       "",
				deviceName:     "",
				sshConfig:      "",
				printSSHConfig: false,
				tunnelPorts:    intSlice([]int{}),
			},
			expectedRemoteHost:     "fake.remote.host",
			expectedDeviceIP:       fakeFoundIPAddr,
			expectedDeviceName:     fakeFoundName,
			expectedSSHConfig:      "",
			expectedPrintSSHConfig: false,
			sdk: testSDKProperties{
				DataPath: fakeHomePath,
				device: sdkcommon.DeviceConfig{
					DeviceIP:   fakeFoundIPAddr,
					DeviceName: fakeFoundName,
				},
			},
		},
		{
			tunnelCmd: &tunnelCmd{
				remoteHost:     "fake.remote.host",
				deviceIP:       "",
				deviceName:     "",
				sshConfig:      fakeSSHConfigPath,
				printSSHConfig: true,
				tunnelPorts:    intSlice([]int{}),
			},
			expectedRemoteHost:     "fake.remote.host",
			expectedDeviceIP:       fakeFoundIPAddr,
			expectedDeviceName:     fakeFoundName,
			expectedSSHConfig:      fakeSSHConfigPath,
			expectedPrintSSHConfig: true,
			sdk: testSDKProperties{DataPath: fakeHomePath, device: sdkcommon.DeviceConfig{
				DeviceIP:   fakeFoundIPAddr,
				DeviceName: fakeFoundName,
			}},
		},
	}
	for _, test := range tests {
		ctx := context.Background()
		if _, err := test.tunnelCmd.parseFlags(ctx, test.sdk); err != nil {
			t.Errorf("error calling parseFlags: %s", err)
		}
		if test.expectedRemoteHost != test.tunnelCmd.remoteHost {
			t.Errorf("got remote host %s, want %s", test.tunnelCmd.remoteHost, test.expectedRemoteHost)
		}
		if test.expectedDeviceIP != test.tunnelCmd.deviceIP {
			t.Errorf("got device IP %s, want %s", test.tunnelCmd.deviceIP, test.expectedDeviceIP)
		}
		if test.expectedDeviceName != test.tunnelCmd.deviceName {
			t.Errorf("got device name %s, want %s", test.tunnelCmd.deviceName, test.expectedDeviceName)
		}
		if test.expectedSSHConfig != "" && test.expectedSSHConfig != test.tunnelCmd.sshConfig {
			t.Errorf("got SSH config path %s, want %s", test.tunnelCmd.sshConfig, test.expectedSSHConfig)
		}
		if test.expectedPrintSSHConfig != test.tunnelCmd.printSSHConfig {
			t.Errorf("got print SSH config boolean %t, want %t", test.tunnelCmd.printSSHConfig, test.expectedPrintSSHConfig)
		}
	}
}

func TestRemoteHostCache(t *testing.T) {
	fakeHomePath := t.TempDir()
	tunnelCmdNoRemote := &tunnelCmd{
		remoteHost:     "",
		deviceIP:       "",
		deviceName:     fakeFoundName,
		sshConfig:      "",
		printSSHConfig: false,
		tunnelPorts:    intSlice([]int{}),
	}
	expectedErrMsg := "No remote host provided. Please add the '-remote-host' flag"

	ctx := context.Background()
	sdk := &testSDKProperties{
		DataPath: fakeHomePath,
		device: sdkcommon.DeviceConfig{
			DeviceIP:   fakeFoundIPAddr,
			DeviceName: fakeFoundName,
		},
	}

	_, err := tunnelCmdNoRemote.parseFlags(ctx, sdk)
	if err.Error() != expectedErrMsg {
		t.Fatalf("parseFlags() got error %s, expected %s", err, expectedErrMsg)
	}

	tunnelCmdWithRemote := &tunnelCmd{
		remoteHost:     "fake.remote.host",
		deviceIP:       "",
		deviceName:     fakeFoundName,
		sshConfig:      "",
		printSSHConfig: false,
		tunnelPorts:    intSlice([]int{}),
	}

	_, err = tunnelCmdWithRemote.parseFlags(ctx, sdk)
	if err != nil {
		t.Fatalf("error calling parseFlags: %s", err)
	}

	_, err = tunnelCmdNoRemote.parseFlags(ctx, sdk)
	if err != nil {
		t.Fatalf("expected cached remote-host to be used when calling parseFlags but got error: %s", err)
	}

	tunnelCmdWithDifferentRemote := &tunnelCmd{
		remoteHost:     "different.remote.host",
		deviceIP:       "",
		deviceName:     fakeFoundName,
		sshConfig:      "",
		printSSHConfig: false,
		tunnelPorts:    intSlice([]int{}),
	}

	contents, err := tunnelCmdWithDifferentRemote.parseFlags(ctx, sdk)
	if err != nil {
		t.Fatalf("error calling parseFlags: %s", err)
	}
	if strings.Contains(string(contents), "fake.remote.host") {
		t.Fatalf("did not expect 'fake.remote.host' to be in ssh config")
	}
	if !strings.Contains(string(contents), "different.remote.host") {
		t.Fatalf("expected 'different.remote.host' to be in ssh config")
	}
}

func TestNegativeParseFlags(t *testing.T) {
	fakeHomePath := t.TempDir()
	var tests = []struct {
		tunnelCmd      *tunnelCmd
		sdk            testSDKProperties
		expectedErrMsg string
	}{
		{
			tunnelCmd: &tunnelCmd{
				remoteHost:     "",
				deviceIP:       "",
				deviceName:     "",
				sshConfig:      "",
				printSSHConfig: false,
				tunnelPorts:    intSlice([]int{}),
			},
			sdk:            testSDKProperties{DataPath: fakeHomePath},
			expectedErrMsg: "No remote host provided. Please add the '-remote-host' flag",
		},
		{
			tunnelCmd: &tunnelCmd{
				remoteHost:     "fake.remote.host",
				deviceIP:       "",
				deviceName:     fakeNotFoundName,
				sshConfig:      "",
				printSSHConfig: false,
				tunnelPorts:    intSlice([]int{}),
			},
			sdk: testSDKProperties{
				DataPath: fakeHomePath,
				device: sdkcommon.DeviceConfig{
					DeviceIP:   fakeFoundIPAddr,
					DeviceName: fakeFoundName,
				},
				err: fmt.Errorf("no devices found"),
			},
			expectedErrMsg: "no devices found",
		},
		{
			tunnelCmd: &tunnelCmd{
				remoteHost:     "fake.remote.host",
				deviceIP:       fakeFoundIPAddr,
				deviceName:     fakeFoundName,
				sshConfig:      "",
				printSSHConfig: true,
				tunnelPorts:    intSlice([]int{22, 8888, 9060}),
			},
			sdk: testSDKProperties{
				DataPath: fakeHomePath,
				device: sdkcommon.DeviceConfig{
					DeviceIP:   fakeFoundIPAddr,
					DeviceName: fakeFoundName,
				},
			},
			expectedErrMsg: "Could not generate default SSH config: Cannot create SSH config with protected ports: 22",
		},
	}
	for _, test := range tests {
		ctx := context.Background()
		_, err := test.tunnelCmd.parseFlags(ctx, test.sdk)
		if err == nil {
			t.Errorf("no error calling parseFlags but expected error: %s", test.expectedErrMsg)
		} else if err.Error() != test.expectedErrMsg {
			t.Errorf("parseFlags() got error %s, want %s", err, test.expectedErrMsg)
		}
	}
}

func TestIsThisFailedConnectionPortMessage(t *testing.T) {
	tests := []struct {
		message        string
		expectedOutput bool
	}{
		{
			message:        "connect_to fe80::f039:18e6:6e66:bee8%en8 port 80: failed.",
			expectedOutput: true,
		}, {
			message:        "connect_to fe80::f039:18e6:6e66:bee8%en8 port 9080: failed.",
			expectedOutput: true,
		}, {
			message:        "connect_to fe80::f039:18e6:6e66:bee8%en8 port: failed.",
			expectedOutput: false,
		}, {
			message:        "some other error",
			expectedOutput: false,
		},
	}
	for _, test := range tests {
		if output := isThisFailedConnectionPortMessage(test.message); output != test.expectedOutput {
			t.Errorf("isThisFailedConnectionPortMessage() got: %t, want %t", output, test.expectedOutput)
		}
	}
}

func TestValidHostname(t *testing.T) {
	validNames := []string{
		"host.com",
		"my.host.com",
		"my1st.host.com",
		"my.l33t.com",
		"google.org",
		"long.name.with.multiple.segments.google.com",
		"d.lax.corp.google.com",
		"host-with_different_90-names.lax.corp.google.com",
		"123.123.22.2",
	}
	for i, name := range validNames {
		t.Run(fmt.Sprintf("TestValidHostname %d", i), func(t *testing.T) {
			if !validHostname(name) {
				t.Fatalf("%s should be valid but is not", name)
			}
		})
	}
}

func TestInvalidHostname(t *testing.T) {
	invalidNames := []string{
		"",                 // Empty.
		"host",             // No domain.
		"user@my.host.com", // Invalid to have user prefix.
	}
	for i, name := range invalidNames {
		t.Run(fmt.Sprintf("TestInvalidHostname %d", i), func(t *testing.T) {
			if validHostname(name) {
				t.Fatalf("%s should be not be valid", name)
			}
		})
	}
}
