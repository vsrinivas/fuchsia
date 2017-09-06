// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	"encoding/json"
	"io/ioutil"
)

type Config struct {
	SSID         string
	ScanInterval int
	Password     string
}

func NewConfig() *Config {
	return &Config{}
}

func ReadConfigFromFile(path string) (*Config, error) {
	cfgBytes, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, err
	}

	cfg := NewConfig()
	err = json.Unmarshal(cfgBytes, cfg)
	return cfg, err
}
