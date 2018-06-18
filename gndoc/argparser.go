// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gndoc

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"strings"
)

// Arg holds the information directly parsed from the json output of `gn args <build> --list --json`.
type Arg struct {
	Name       string   `json:"name"`
	CurrentVal argValue `json:"current"`
	DefaultVal argValue `json:"default"`
	Comment    string   `json:"comment"`
	Key        string   `json: "-"`
}

// ArgValue holds a value, its filepath and line number, and the build associated with the value.
type argValue struct {
	Val  interface{} `json:"value"`
	File string      `json:"file"`
	Line int         `json:"line"`
}

// ParseGNArgs runs the necessary gn commands and decodes the json output into a channel of GNArgs.
func ParseGNArgs(ctx context.Context, inputFiles []string, keyArgs []string) (<-chan Arg, <-chan error) {
	args := make(chan Arg)
	errs := make(chan error, 1)
	go func() {
		defer func() {
			close(args)
			close(errs)
		}()
		for _, input := range inputFiles {
			select {
			case <-ctx.Done():
				return
			default:
				err := parseJson(input, keyArgs, args)
				if err != nil {
					errs <- err
					return
				}
			}
		}
	}()
	return args, errs
}

func parseJson(input string, keyArgs []string, out chan<- Arg) error {
	// Open the json file.
	file, err := os.Open(input)
	if err != nil {
		return err
	}
	defer file.Close()

	// Decode the json into GNArgs.
	var gnArgs []Arg
	decoder := json.NewDecoder(file)
	if err := decoder.Decode(&gnArgs); err != nil {
		return err
	}

	// Mark the args with the relevant key and send to channel.
	key := getKey(gnArgs, keyArgs)
	for _, arg := range gnArgs {
		arg.Key = key
		out <- arg
	}

	return nil
}

// TODO: make sure this is sorted before stringifying
// getKey searches the decoded json for the flagged keys and builds the marker string.
func getKey(args []Arg, keys []string) string {
	keyVals := make([]string, len(keys))
	for _, arg := range args {
		for idx, key := range keys {
			if arg.Name == key {
				keyVals[idx] = fmt.Sprintf("%s = %v", key, arg.CurrentVal.Val)
			}
		}
	}
	return strings.Join(keyVals, ", ")
}
