// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import "fmt"

var HardwareDisplayGidl = GidlFile{
	Filename: "hardware_display.gidl",
	Gen:      GidlGenHardwareDisplay,
	Benchmarks: []Benchmark{
		{
			Name: "HardwareDisplay/OnVsyncEvent/Image64",
			Config: Config{
				"num_images": 32,
			},
		},
	},
}

func GidlGenHardwareDisplay(conf Config) (string, error) {
	numImages := conf["num_images"].(int)

	return fmt.Sprintf(`OnVsyncEvent{
	display_id: 1,
	timestamp: 1,
	images: [
		%[1]s
	],
}`, gidlBytes(numImages)), nil
}
