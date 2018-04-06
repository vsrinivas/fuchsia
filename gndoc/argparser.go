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

// GNArg holds the information directly parsed from the json output of `gn args <build> --list --json`.
type GNArg struct {
	Name       string   `json:"name"`
	CurrentVal ArgValue `json:"current"`
	DefaultVal ArgValue `json:"default"`
	Comment    string   `json:"comment"`
	Key        string   `json: "-"`
}

// ParseGNArgs runs the necessary gn commands and decodes the json output into a channel of GNArgs.
func ParseGNArgs(ctx context.Context, inputFiles []string, keyArgs []string) (<-chan GNArg, <-chan string, <-chan error) {
	args := make(chan GNArg)
	// Buffer the keys we find so that we don't block the args.
	keys := make(chan string, len(inputFiles))
	errs := make(chan error, 1)
	go func() {
		defer func() {
			close(args)
			close(keys)
			close(errs)
		}()
		for _, input := range inputFiles {
			select {
			case <-ctx.Done():
				return
			default:
				key, err := parseJson(input, keyArgs, args)
				if err != nil {
					errs <- err
					return
				}
				keys <- key
			}
		}
	}()
	return args, keys, errs
}

func parseJson(input string, keyArgs []string, out chan<- GNArg) (string, error) {
	// Open the json file.
	file, err := os.Open(input)
	if err != nil {
		return "", err
	}
	defer file.Close()

	// Decode the json into GNArgs.
	var gnArgs []GNArg
	decoder := json.NewDecoder(file)
	if err := decoder.Decode(&gnArgs); err != nil {
		return "", err
	}

	// Mark the args with the relevant key and send to channel.
	key := getKey(gnArgs, keyArgs)
	for _, arg := range gnArgs {
		arg.Key = key
		out <- arg
	}

	return key, nil
}

// getKey searches the decoded json for the flagged keys and builds the marker string.
func getKey(args []GNArg, keys []string) string {
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
