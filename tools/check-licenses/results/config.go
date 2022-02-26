// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package results

type ConfigInterface interface {
	Merge()
}

type MetricsInterface interface {
	Counts() map[string]int
	Values() map[string][]string
}

type ResultsConfig struct {
	OutputDir string          `json:"outputdir"`
	Outputs   map[string]bool `json:"outputs"`
}

var Config *ResultsConfig

func init() {
	Config = &ResultsConfig{}
}

func (c *ResultsConfig) Merge(other *ResultsConfig) {
	if c.OutputDir == "" {
		c.OutputDir = other.OutputDir
	}
	for k := range other.Outputs {
		c.Outputs[k] = true
	}
}
