// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"

	checklicenses "go.fuchsia.dev/fuchsia/tools/check-licenses"
)

const configFile = "tools/check-licenses/config/config.json"

func main() {
	var config checklicenses.Config
	configJson := flag.String("config", configFile, "Location of config.json")
	if err := config.Init(configJson); err != nil {
		fmt.Println(err.Error())
		return
	}
	flag.StringVar(&config.Target, "target", config.Target, "Options: {all, <target>}")
	if err := checklicenses.Walk(&config); err != nil {
		fmt.Println(err.Error())
	}
}
