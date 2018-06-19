// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package wlan

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
)

// If we remove the wlanConfigUser, remove "persistent-storage" access from meta/sandbox.
const wlanConfigUser = "/data/wlan_config_user.json"

type Config struct {
	SSID         string `json:"SSID"`
	ScanInterval int    `json:"ScanInternal"`
	Password     string `json:"Password"`
	BSSID        string `json:"BSSID"`
}

func NewConfig() *Config {
	return &Config{}
}

func ReadConfigUser() (*Config, error) {
	return ReadConfigFromFile(wlanConfigUser)
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

func (cfg *Config) SaveConfigUser() error {
	cfgBytes, err := json.Marshal(*cfg)
	if err != nil {
		fmt.Printf("Cannot marshal config data into json\n")
		return err;
	}

	err = ioutil.WriteFile(wlanConfigUser, cfgBytes, 0)
	if err != nil {
		fmt.Printf("Error writing the config file: %s\nError: %+v\n", wlanConfigUser, err)
	} else {
		fmt.Printf("Successfully saved config to %s\n", wlanConfigUser)
	}
	return err
}
