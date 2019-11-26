// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sl4f

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
)

var ErrNotSupported error = errors.New("not supported")

type Configuration string

const (
	ConfigurationA        Configuration = "a"
	ConfigurationB                      = "b"
	ConfigurationRecovery               = "recovery"
)

type ConfigurationStatus string

const (
	ConfigurationStatusHealthy    ConfigurationStatus = "healthy"
	ConfigurationStatusPending                        = "pending"
	ConfigurationStatusUnbootable                     = "unbootable"
)

type Asset string

const (
	AssetKernel               Asset = "kernel"
	AssetVerifiedBootMetadata       = "verified_boot_metadata"
)

// PaverQueryActiveConfiguration determines the active boot configuration of the target, if supported.
func (c *Client) PaverQueryActiveConfiguration() (Configuration, error) {
	var raw_response json.RawMessage

	if err := c.call(context.Background(), "paver.QueryActiveConfiguration", nil, &raw_response); err != nil {
		return "", err
	}

	// raw_response is either a json encoded string literal or an object
	// with a single key called "Success".
	if string(raw_response) == `"not_supported"` {
		return "", ErrNotSupported
	}
	var response struct {
		Success Configuration `json:"success"`
	}
	if err := json.Unmarshal(raw_response, &response); err != nil {
		return "", fmt.Errorf("error unmashaling response: %s", raw_response)
	}

	switch response.Success {
	case ConfigurationA:
		return ConfigurationA, nil
	case ConfigurationB:
		return ConfigurationB, nil
	case ConfigurationRecovery:
		return ConfigurationRecovery, nil
	default:
		return "", fmt.Errorf("Unknown configuration name: %s", response.Success)
	}
}

// PaverQueryConfigurationStatus determines the status of the given boot slot, if supported.
func (c *Client) PaverQueryConfigurationStatus(configuration Configuration) (ConfigurationStatus, error) {
	request := struct {
		Configuration Configuration `json:"configuration"`
	}{
		Configuration: configuration,
	}
	var raw_response json.RawMessage

	if err := c.call(context.Background(), "paver.QueryConfigurationStatus", &request, &raw_response); err != nil {
		return "", err
	}

	// raw_response is either a json encoded string literal or an object
	// with a single key called "Success".
	if string(raw_response) == `"not_supported"` {
		return "", ErrNotSupported
	}
	var response struct {
		Success ConfigurationStatus `json:"success"`
	}
	if err := json.Unmarshal(raw_response, &response); err != nil {
		return "", fmt.Errorf("error unmashaling response: %s", raw_response)
	}

	switch response.Success {
	case ConfigurationStatusHealthy:
		return ConfigurationStatusHealthy, nil
	case ConfigurationStatusPending:
		return ConfigurationStatusPending, nil
	case ConfigurationStatusUnbootable:
		return ConfigurationStatusUnbootable, nil
	default:
		return "", fmt.Errorf("Unknown configuration status name: %s", response.Success)
	}
}

// PaverReadAsset reads the requested asset from the device.
func (c *Client) PaverReadAsset(configuration Configuration, asset Asset) ([]byte, error) {
	request := struct {
		Configuration Configuration `json:"configuration"`
		Asset         Asset         `json:"asset"`
	}{
		Configuration: configuration,
		Asset:         asset,
	}
	var response string

	if err := c.call(context.Background(), "paver.ReadAsset", &request, &response); err != nil {
		return nil, err
	}

	return base64.StdEncoding.DecodeString(response)
}
