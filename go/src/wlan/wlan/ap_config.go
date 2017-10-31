// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	"encoding/json"
	"io/ioutil"
)

type APConfig struct {
	SSID         string
	BeaconPeriod int
	DTIMPeriod   int
	Active       bool
}

func NewAPConfig() *APConfig {
	return &APConfig{}
}

func ReadAPConfigFromFile(path string) (*APConfig, error) {
	cfgBytes, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, err
	}

	cfg := NewAPConfig()
	err = json.Unmarshal(cfgBytes, cfg)
	return cfg, err
}
