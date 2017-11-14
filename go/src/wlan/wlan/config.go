// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
)

type Config struct {
	SSID         string `json:"SSID"`
	ScanInterval int    `json:"ScanInternal"`
	Password     string `json:"Password"`
	BSSID        string `json:"BSSID"`
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
	fmt.Printf("Done reading the config file: %s\n", path)
	return cfg, err
}
