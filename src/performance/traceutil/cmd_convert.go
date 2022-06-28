// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"

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
	return "Converts an FXT trace to JSON."
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
		extension := fullExt(filename)
		basename := filename[0 : len(filename)-len(extension)]
		if extension == ".fxt" || extension == ".fxt.gz" {
			jsonGenerator := getJsonGenerator()
			jsonFilename := basename + ".json"
			err := convertToJson(jsonGenerator, extension == ".fxt.gz", jsonFilename, filename)
			if err != nil {
				ret = subcommands.ExitFailure
				continue
			}
		} else {
			fmt.Printf("Skipping file with unknown extension: %s (%s)\n", filename, extension)
		}
	}

	return ret
}
