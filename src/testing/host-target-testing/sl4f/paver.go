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
	"strings"
)

var ErrNotSupported error = errors.New("not supported")
var ErrInvalidPaverMethod error = errors.New("Invalid paver facade method")

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

func (c *Client) paverQueryConfiguration(ctx context.Context, methodName string) (Configuration, error) {
	var rawResponse json.RawMessage

	if err := c.call(ctx, methodName, nil, &rawResponse); err != nil {
		if strings.HasPrefix(err.Error(), "error from server: Invalid paver facade method") {
			// TODO(60425): remove this error-string-checking hack once our
			// min version supports all the paver methods.
			return "", ErrInvalidPaverMethod
		}
		return "", err
	}

	// rawResponse is either a json encoded string literal or an object
	// with a single key called "Success".
	if string(rawResponse) == `"not_supported"` {
		return "", ErrNotSupported
	}
	var response struct {
		Success Configuration `json:"success"`
	}
	if err := json.Unmarshal(rawResponse, &response); err != nil {
		return "", fmt.Errorf("error unmashaling response: %s", rawResponse)
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

// PaverQueryActiveConfiguration determines the active boot configuration of the target, if supported.
func (c *Client) PaverQueryActiveConfiguration(ctx context.Context) (Configuration, error) {
	return c.paverQueryConfiguration(ctx, "paver.QueryActiveConfiguration")
}

// PaverQueryCurrentConfiguration determines the active boot configuration of the target, if supported.
func (c *Client) PaverQueryCurrentConfiguration(ctx context.Context) (Configuration, error) {
	return c.paverQueryConfiguration(ctx, "paver.QueryCurrentConfiguration")
}

// PaverQueryConfigurationStatus determines the status of the given boot slot, if supported.
func (c *Client) PaverQueryConfigurationStatus(ctx context.Context, configuration Configuration) (ConfigurationStatus, error) {
	request := struct {
		Configuration Configuration `json:"configuration"`
	}{
		Configuration: configuration,
	}
	var raw_response json.RawMessage

	if err := c.call(ctx, "paver.QueryConfigurationStatus", &request, &raw_response); err != nil {
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
func (c *Client) PaverReadAsset(ctx context.Context, configuration Configuration, asset Asset) ([]byte, error) {
	request := struct {
		Configuration Configuration `json:"configuration"`
		Asset         Asset         `json:"asset"`
	}{
		Configuration: configuration,
		Asset:         asset,
	}
	var response string

	if err := c.call(ctx, "paver.ReadAsset", &request, &response); err != nil {
		return nil, err
	}

	return base64.StdEncoding.DecodeString(response)
}
