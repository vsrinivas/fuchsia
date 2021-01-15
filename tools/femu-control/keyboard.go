// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"strings"

	"github.com/google/subcommands"
	fg "go.fuchsia.dev/fuchsia/tools/femu-control/femu-grpc"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

type keyboardCmd struct {
	eventType   string
	keySequence []string
}

func (*keyboardCmd) Name() string     { return "keyboard" }
func (*keyboardCmd) Synopsis() string { return "Send keyboard events" }
func (*keyboardCmd) Usage() string {
	return `keyboard: Send keyboard event to FEMU.

Usage:

keyboard [-type DOWN|UP|PRESS] <key_sequence>

<key_sequence> is a string of key names concatenanted by character '+'.
Order of keys represents the order they are pressed on the keyboard.
Examples:
	"A"
	"Enter"
	"Control+Alt+F3"
	"Meta+Z"

Flags:
`
}

func (c *keyboardCmd) SetFlags(f *flag.FlagSet) {
	f.StringVar(&c.eventType, "event", "PRESS", "keyboard event type: DOWN, UP or PRESS")
}

func (c *keyboardCmd) ValidateArgs() error {
	if len(c.keySequence) == 0 {
		return fmt.Errorf("key sequence missing")
	}

	switch c.eventType {
	case "DOWN", "UP", "PRESS":
		break
	default:
		return fmt.Errorf("unknown event type: %s", c.eventType)
	}

	return nil
}

func (c *keyboardCmd) run(ctx context.Context) error {
	client := ctx.Value("client").(fg.FemuGrpcClientInterface)
	if client == nil {
		return fmt.Errorf("FEMU gRPC client not found")
	}

	var err error
	switch c.eventType {
	case "DOWN":
		err = client.KeyDown(c.keySequence)
	case "UP":
		err = client.KeyUp(c.keySequence)
	case "PRESS":
		err = client.KeyPress(c.keySequence)
	}
	if err != nil {
		return fmt.Errorf("FEMU gRPC client error: %v", err)
	}

	return nil
}

func (c *keyboardCmd) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if f.Arg(0) != "" {
		c.keySequence = strings.Split(f.Arg(0), "+")
	}
	if err := c.ValidateArgs(); err != nil {
		logger.Errorf(ctx, err.Error())
		return subcommands.ExitUsageError
	}

	if err := c.run(ctx); err != nil {
		logger.Errorf(ctx, err.Error())
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}
