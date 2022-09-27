// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package world

type WorldConfig struct {
	Target     string `json:"target"`
	FuchsiaDir string `json:"fuchsiaDir"`
	BuildDir   string `json:"buildDir"`
	GnPath     string `json:"gnPath"`

	BuildInfoVersion string `json:"buildInfoVersion"`
	BuildInfoProduct string `json:"buildInfoProduct"`
	BuildInfoBoard   string `json:"buildInfoBoard"`

	Filters []string `json:"filters"`

	DiffNotice string `json:"diffnotice"`
}

var Config *WorldConfig

func NewConfig() *WorldConfig {
	return &WorldConfig{
		Filters: make([]string, 0),
	}
}

func (c *WorldConfig) Merge(other *WorldConfig) {
	if c.Target == "" {
		c.Target = other.Target
	}
	if c.FuchsiaDir == "" {
		c.FuchsiaDir = other.FuchsiaDir
	}
	if c.BuildDir == "" {
		c.BuildDir = other.BuildDir
	}
	if c.GnPath == "" {
		c.GnPath = other.GnPath
	}
	if c.DiffNotice == "" {
		c.DiffNotice = other.DiffNotice
	}
	c.Filters = append(c.Filters, other.Filters...)
	if c.BuildInfoVersion == "" {
		c.BuildInfoVersion = other.BuildInfoVersion
	}
	if c.BuildInfoProduct == "" {
		c.BuildInfoProduct = other.BuildInfoProduct
	}
	if c.BuildInfoBoard == "" {
		c.BuildInfoBoard = other.BuildInfoBoard
	}
}
