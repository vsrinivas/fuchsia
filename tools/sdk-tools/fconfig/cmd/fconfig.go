// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/sdk-tools/sdkcommon"
)

//SDKProvider interface for mocking in tests.
type SDKProvider interface {
	GetDeviceConfiguration(name string) (sdkcommon.DeviceConfig, error)
	GetFuchsiaProperty(deviceName string, propertyName string) (string, error)
	SaveDeviceConfiguration(newConfig sdkcommon.DeviceConfig) error
	IsValidProperty(property string) bool
	GetDeviceConfigurations() ([]sdkcommon.DeviceConfig, error)
}

func main() {
	var sdk = sdkcommon.SDKProperties{}
	err := sdk.Init()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Could not initialize SDK: %v", err)
		os.Exit(1)
	}

	helpFlag := flag.Bool("help", false, "Show the usage message")
	flag.String("bucket", "", "Specify the GCS bucket to be retrieve prebuilt images and packages.")
	flag.String("image", "", "Specify the GCS image to be retrieve prebuilt images and packages.")
	flag.String("device-ip", "", "Specify the IP address for the target device.")
	flag.String("ssh-port", "", "Specify the port number to use for SSH to the target device.")
	flag.String("package-repo", "", "Override the default package repository path for the target device.")
	flag.String("package-port", "", "Override the default port number use by the package server for this device.")
	flag.Bool("default", false, "Marks this device as the default device.")

	flag.Parse()

	if *helpFlag || len(flag.Args()) == 0 {
		usage(sdk)
		os.Exit(0)
	}

	cmd := flag.Args()[0]

	switch cmd {
	case "set-device":
		var deviceName string

		if len(flag.Args()) > 1 {
			deviceName = flag.Args()[1]
		} else {
			usage(sdk)
			log.Fatal("set-device requires device-name")
		}
		// Parse options after the device name
		if len(flag.Args()) > 2 {
			flag.CommandLine.Parse(flag.Args()[2:])
		}
		newProperties := make(map[string]string)
		flag.Visit(func(f *flag.Flag) {
			newProperties[f.Name] = f.Value.String()
		})

		err := doSetDevice(&sdk, deviceName, newProperties)
		if err != nil {
			log.Fatal(err)
		}
	case "get":
		var property string
		if len(flag.Args()) > 1 {
			property = flag.Args()[1]
		} else {
			usage(sdk)
			log.Fatal("Missing property name for 'get'")
		}
		value, err := doGet(sdk, property)
		if err != nil {
			log.Fatal(err)
		}
		fmt.Println(value)
	case "get-all":
		device := ""
		// Use the device-name if specified on the command line,
		// otherwise use the default device name
		if len(flag.Args()) > 1 {
			device = flag.Args()[1]
		} else {
			device, err = sdk.GetDefaultDeviceName()
			if err != nil {
				log.Fatal(err)
			}
		}
		err := doList(sdk, device)
		if err != nil {
			log.Fatal(err)
		}

	case "list":
		err := doList(sdk, "")
		if err != nil {
			log.Fatal(err)
		}
	case "remove-device":
		var device string
		if len(flag.Args()) > 1 {
			device = flag.Args()[1]
		} else {
			usage(sdk)
			log.Fatal("Missing device-name for 'remove-device'")
		}
		err := sdk.RemoveDeviceConfiguration(device)
		if err != nil {
			log.Fatal(err)
		}
	default:
		usage(sdk)
		log.Fatalf("Unknown command: %s", cmd)
	}
	os.Exit(0)
}

const usageText = `
	set-device <device-name>  Sets the device properties supplied in options for the device matching the name.
	get [<device-name>.]<property-name>: Prints the value of the property or empty string if not found.
	get-all [<device-name>] prints all settings for the default device, or the device-name if provided.
	list: Lists all settings.
	remove-device <device-name>  Removes the configuration for the given target device.

	Options:
	[--bucket <bucket>]  - specify the GCS bucket to be retrieve prebuilt images and packages.
	[--image <image>]    - specify the GCS image to be retrieve prebuilt images and packages.
	[--device-ip <addr>] - specify the IP address for the target device
	[--ssh-port <port>]  - specify the port number to use for SSH to the target device.
	[--package-repo <path>]  - override the default package repository path for the target device.
	[--package-port <port>]  - override the default port number use by the package server for this device.
	[--default]              - marks this device as the default device.
`

func usage(sdk sdkcommon.SDKProperties) {
	fmt.Printf("Usage: %s set-device <device-name> [options]\n", filepath.Base(os.Args[0]))
	fmt.Printf(usageText)
}

func doGet(sdk SDKProvider, property string) (string, error) {
	deviceName := ""
	propertyName := property

	// split the device name if present from the property
	parts := strings.Split(property, ".")
	if len(parts) > 1 {
		deviceName = parts[0]
		propertyName = strings.Join(parts[1:], ".")
	}
	if sdk.IsValidProperty(propertyName) {
		value, err := sdk.GetFuchsiaProperty(deviceName, propertyName)
		if err != nil {
			return "", err
		}
		return value, nil
	} else {
		return "", fmt.Errorf("Invalid property name: %s", property)
	}
}

func doSetDevice(sdk SDKProvider, deviceName string, propertyMap map[string]string) error {
	currentConfig, err := sdk.GetDeviceConfiguration(deviceName)
	if err != nil {
		return err
	}

	if len(propertyMap) == 0 {
		return fmt.Errorf("No options  set for %v", deviceName)
	}

	currentConfig.DeviceName = deviceName

	for key, value := range propertyMap {
		switch key {
		case "bucket":
			currentConfig.Bucket = value
		case "device-ip":
			currentConfig.DeviceIP = value
		case "image":
			currentConfig.Image = value
		case "default":
			currentConfig.IsDefault, _ = strconv.ParseBool(value)
		case "package-port":
			currentConfig.PackagePort = value
		case "package-repo":
			currentConfig.PackageRepo = value
		case "ssh-port":
			currentConfig.SSHPort = value
		}
	}
	return sdk.SaveDeviceConfiguration(currentConfig)
}

func doList(sdk SDKProvider, deviceName string) error {
	var (
		configList []sdkcommon.DeviceConfig
		err        error
	)
	if deviceName == "" {
		configList, err = sdk.GetDeviceConfigurations()
		if err != nil {
			return err
		}
	} else {
		config, err := sdk.GetDeviceConfiguration(deviceName)
		if err != nil {
			return err
		}
		configList = append(configList, config)
	}
	if len(configList) == 0 {
		fmt.Fprintln(os.Stderr, "No configurations set")
		return nil
	}
	for _, config := range configList {
		data, _ := json.MarshalIndent(config, "", "")
		fmt.Println(string(data))
	}
	return nil
}
