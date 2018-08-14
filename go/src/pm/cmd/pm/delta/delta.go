// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package delta contains the `pm delta` command
package delta

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"text/tabwriter"

	"fuchsia.googlesource.com/pm/build"
	"github.com/dustin/go-humanize"
)

const usage = `Usage: %s delta [OPTIONS] SOURCE_SNAPSHOT TARGET_SNAPSHOT
compare two package set snapshots`

type deltaConfig struct {
	// input/output paths
	sourcePath string
	targetPath string
	outputPath string

	// stdout display options (ignored if outputPath is specified)
	summary           bool
	packages          bool
	blobs             bool
	packageCount      uint
	blobCount         uint
	detailed          bool
	showSectionHeader bool
}

func countTrueValues(bools ...bool) int {
	sum := 0
	for _, value := range bools {
		if value {
			sum++
		}
	}
	return sum
}

func parseConfig(args []string) (*deltaConfig, error) {
	fs := flag.NewFlagSet("delta", flag.ExitOnError)

	var c deltaConfig

	fs.StringVar(&c.outputPath, "output", "", "Write delta as JSON to the provided path instead of writing to stdout ('-' to write json to stdout)")
	fs.BoolVar(&c.detailed, "detailed", false, "Include all package and blob statistics, instead of just the top few")
	fs.BoolVar(&c.summary, "summary", false, "Show summary of update statistics")
	fs.BoolVar(&c.packages, "packages", false, "Show per-package statistics")
	fs.BoolVar(&c.blobs, "blobs", false, "Show per-blob statistics")
	fs.UintVar(&c.packageCount, "package_count", 0, "Show up to N packages with largest updates (implies --packages)")
	fs.UintVar(&c.blobCount, "blob_count", 0, "Show up to N blobs with largest updates (implies --blobs)")

	fs.Usage = func() {
		fmt.Fprintf(fs.Output(), usage, filepath.Base(os.Args[0]))
		fmt.Fprintln(fs.Output())
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		return nil, err
	}

	if fs.NArg() != 2 {
		fmt.Fprintf(fs.Output(), "expected source and target file paths, got %s\n", fs.Args())
		fs.Usage()
		os.Exit(1)
	}
	c.sourcePath = fs.Arg(0)
	c.targetPath = fs.Arg(1)

	// Requesting a specific number of results implies showing that section
	if c.packageCount == 0 {
		c.packageCount = 5
	} else {
		c.packages = true
	}

	if c.blobCount == 0 {
		c.blobCount = 20
	} else {
		c.blobs = true
	}

	// Default to showing all sections if none are specifically requested
	if !c.summary && !c.packages && !c.blobs {
		c.summary = true
		c.packages = true
		c.blobs = true
	}

	// Show section headers if more than one section will be shown
	c.showSectionHeader = countTrueValues(c.summary, c.packages, c.blobs) > 1

	return &c, nil
}

// Run executes the delta command
func Run(cfg *build.Config, args []string) error {
	config, err := parseConfig(args)
	if err != nil {
		return err
	}

	source, err := build.LoadSnapshot(config.sourcePath)
	if err != nil {
		return err
	}

	target, err := build.LoadSnapshot(config.targetPath)
	if err != nil {
		return err
	}

	delta := build.DeltaSnapshots(source, target)

	if config.outputPath != "" {
		var encoder *json.Encoder

		if config.outputPath == "-" {
			encoder = json.NewEncoder(os.Stdout)
			encoder.SetIndent("", "  ")
		} else {
			file, err := os.Create(config.outputPath)
			if err != nil {
				return err
			}
			defer file.Close()
			encoder = json.NewEncoder(file)
		}

		if err := encoder.Encode(delta); err != nil {
			return err
		}
	} else {
		if config.summary {
			fmt.Printf("Source size: %v\n", humanize.IBytes(delta.SourceSize))
			fmt.Printf("Target size: %v\n", humanize.IBytes(delta.TargetSize))
			fmt.Printf("Discard size: %v\n", humanize.IBytes(delta.DiscardSize))
			fmt.Printf("Keep size: %v\n", humanize.IBytes(delta.UnchangedSize))
			fmt.Printf("Download size: %v\n", humanize.IBytes(delta.DownloadSize))
			fmt.Println()
		}

		if config.packages {
			top := int(config.packageCount)
			if top > len(delta.Packages) || config.detailed {
				top = len(delta.Packages)
			}
			if config.showSectionHeader {
				if config.detailed {
					fmt.Printf("Per-package stats:\n\n")
				} else {
					fmt.Printf("Top %v package(s) with largest update size:\n\n", top)
				}
			}

			w := tabwriter.NewWriter(os.Stdout, 1, 0, 2, ' ', 0)
			fmt.Fprintln(w, "Discard\tKeep\tDownload\tName")
			for _, stats := range delta.Packages[:top] {
				fmt.Fprintf(w, "%v\t%v\t%v\t%s\n",
					humanize.IBytes(stats.DiscardSize),
					humanize.IBytes(stats.UnchangedSize),
					humanize.IBytes(stats.DownloadSize),
					stats.Name,
				)
				for _, info := range stats.AddedBlobs {
					fmt.Fprintf(w, "\t\t%v\t- %s\n",
						humanize.IBytes(info.Size),
						info.PathsDisplay(),
					)
				}

			}
			w.Flush()
			fmt.Println()
		}

		if config.blobs {
			top := int(config.blobCount)
			if top > len(delta.AddedBlobs) || config.detailed {
				top = len(delta.AddedBlobs)
			}
			if config.showSectionHeader {
				if config.detailed {
					fmt.Printf("New blob stats:\n\n")
				} else {
					fmt.Printf("Top %v largest new blob(s):\n\n", top)
				}
			}

			w := tabwriter.NewWriter(os.Stdout, 1, 0, 2, ' ', 0)
			fmt.Fprintln(w, "Download\tName")
			for _, stats := range delta.AddedBlobs[:top] {
				for i, ref := range stats.References {
					if i == 0 {
						fmt.Fprintf(w, "%v\t%s\n", humanize.IBytes(stats.Size), ref)
					} else {
						fmt.Fprintf(w, "\t%s\n", ref)
					}
				}
			}
			w.Flush()
		}
	}

	return nil
}
