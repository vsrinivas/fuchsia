// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

// ProductBundle is a struct that contains a collection of images
// and packages that are outputted from a product build.
type ProductBundle struct {
	Data     Data   `json:"data"`
	SchemaID string `json:"schema_id"`
}

type Data struct {
	DeviceRefs  []string     `json:"device_refs"`
	Images      []*Image     `json:"images"`
	Type        string       `json:"type"`
	Name        string       `json:"name"`
	Packages    []*Package   `json:"packages"`
	Description string       `json:"description"`
	Metadata    [][]Metadata `json:"metadata"`
	Manifests   *Manifests   `json:"manifests,omitempty"`
}

// Product bundle metadata can be an integer, string, or boolean.
type Metadata interface{}

type Manifests struct {
	Flash *FlashManifest `json:"flash,omitempty"`
	Emu   *EmuManifest   `json:"emu,omitempty"`
}

type Package struct {
	Format  string `json:"format"`
	BlobURI string `json:"blob_uri,omitempty"`
	RepoURI string `json:"repo_uri"`
}

type Image struct {
	BaseURI string `json:"base_uri"`
	Format  string `json:"format"`
}

type FlashManifest struct {
	HWRevision string     `json:"hw_revision"`
	Products   []*Product `json:"products"`
}

type Part struct {
	Name      string     `json:"name"`
	Path      string     `json:"path"`
	Condition *Condition `json:"condition,omitempty"`
}

type Condition struct {
	Value    string `json:"value"`
	Variable string `json:"variable"`
}

type OEMFile struct {
	Command string `json:"command"`
	Path    string `json:"path"`
}

type Product struct {
	Name                 string     `json:"name"`
	BootloaderPartitions []*Part    `json:"bootloader_partitions"`
	OEMFiles             []*OEMFile `json:"oem_files"`
	Partitions           []*Part    `json:"partitions"`
}

type EmuManifest struct {
	DiskImages     []string `json:"disk_images"`
	InitialRamdisk string   `json:"initial_ramdisk"`
	Kernel         string   `json:"kernel"`
}
