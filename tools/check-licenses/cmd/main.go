// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"

	"go.fuchsia.dev/fuchsia/scripts/check-licenses/lib"
)

func main() {
	var config lib.Config
	configJson := flag.String("config", "tools/check-licenses/config/config.json", "Location of config.json")
	if err := config.Init(configJson); err != nil {
		fmt.Println(err.Error())
		return
	}
	flag.StringVar(&config.Target, "target", config.Target, "Options: {all, <target>}")
	if err := lib.Walk(&config); err != nil {
		fmt.Println(err.Error())
	}
}
