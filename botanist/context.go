// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"os"
)

const devCtxEnvVar = "DEVICE_CONTEXT"

// DeviceContext contains informationn about a device, which may be set and
// retrieved from the ambient environment.
type DeviceContext struct {
	location string

	// Nodename is the name of the device.
	Nodename string `json:"nodename"`

	// SSHKey is the private SSH user corresponding to an authorized key
	// registered with the device.
	SSHKey string `json:"ssh_key"`
}

// Register stores the the device context value ambiently.
// Unregister must be called to clean this up.
// GetDeviceContext may be called to retrieve the value.
func (devCtx *DeviceContext) Register() error {
	if devCtx.location != "" {
		return nil
	}
	fd, err := ioutil.TempFile("", "botanist")
	if err != nil {
		return err
	}
	defer fd.Close()

	devCtx.location = fd.Name()
	if err = json.NewEncoder(fd).Encode(devCtx); err != nil {
		devCtx.Unregister()
		return err
	}
	return nil
}

// EnvironEntry returns an environment variable pair string "<name>=<value>"
// that may be attached to the environment of subprocesses.
// This method does not modify the current process' environment.
func (devCtx DeviceContext) EnvironEntry() string {
	return fmt.Sprintf("%s=%s", devCtxEnvVar, devCtx.location)
}

// Unregister clears the ambient device context value.
func (devCtx *DeviceContext) Unregister() error {
	defer func() { devCtx.location = "" }()
	return os.Remove(devCtx.location)
}

// GetDeviceContext returns an ambient DeviceContext value, if one has been registered.
func GetDeviceContext() (*DeviceContext, error) {
	location := os.Getenv(devCtxEnvVar)
	if location == "" {
		return nil, errors.New("no device context found")
	}
	fd, err := os.Open(location)
	if err != nil {
		return nil, err
	}
	var devCtx DeviceContext
	if err := json.NewDecoder(fd).Decode(&devCtx); err != nil {
		return nil, err
	}
	devCtx.location = location
	return &devCtx, nil
}
