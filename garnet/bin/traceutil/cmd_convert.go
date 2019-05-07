// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"path"

	"github.com/google/subcommands"
)

type cmdConvert struct {
}

func NewCmdConvert() *cmdConvert {
	cmd := &cmdConvert{}
	return cmd
}

func (*cmdConvert) Name() string {
	return "convert"
}

func (*cmdConvert) Synopsis() string {
	return "Converts a JSON trace to a viewable HTML trace, or an FXT trace to both JSON and HTML."
}

func (cmd *cmdConvert) Usage() string {
	usage := "traceutil convert FILE ...\n"
	return usage
}

func (cmd *cmdConvert) SetFlags(f *flag.FlagSet) {
}

func (cmd *cmdConvert) Execute(_ context.Context, f *flag.FlagSet,
	_ ...interface{}) subcommands.ExitStatus {
	if len(f.Args()) == 0 {
		return subcommands.ExitUsageError
	}

	ret := subcommands.ExitSuccess

	for _, filename := range f.Args() {
		extension := path.Ext(filename)
		basename := filename[0 : len(filename)-len(extension)]
		if extension == ".gz" {
			secondExtension := path.Ext(basename)
			extension = secondExtension + extension
			basename = basename[0 : len(basename)-len(secondExtension)]
		}
		if extension == ".fxt" {
			jsonGenerator := getJsonGenerator()
			jsonFilename := basename + ".json"
			err := convertToJson(jsonGenerator, jsonFilename, filename)
			if err != nil {
				ret = subcommands.ExitFailure
				continue
			}

			htmlGenerator := getHtmlGenerator()
			htmlFilename := basename + ".html"
			err = convertToHtml(htmlGenerator, htmlFilename, "", jsonFilename)
			if err != nil {
				ret = subcommands.ExitFailure
			}
		} else if extension == ".json" || extension == ".json.gz" {
			htmlGenerator := getHtmlGenerator()
			htmlFilename := basename + ".html"
			err := convertToHtml(htmlGenerator, htmlFilename, "", filename)
			if err != nil {
				ret = subcommands.ExitFailure
			}
		} else {
			fmt.Printf("Skipping file with unknown extension: %s (%s)\n", filename, extension)
		}
	}

	return ret
}
