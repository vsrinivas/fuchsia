// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"flag"
	"fmt"
	"net"
	"os"
	"strconv"
	"time"

	subcommands "github.com/google/subcommands"
	fg "go.fuchsia.dev/fuchsia/tools/femu-control/femu-grpc"
)

var (
	server_addr = flag.String("server", "localhost", "server address")
	server_port = flag.Int("port", 5556, "gRPC port")
	timeout     = flag.Duration("timeout", 1*time.Second, "gRPC connection timeout")
)

func newClient() (fg.FemuGrpcClientInterface, error) {
	config := fg.FemuGrpcClientConfig{
		ServerAddr: net.JoinHostPort(*server_addr, strconv.Itoa(*server_port)),
		Timeout:    *timeout,
	}

	client, err := fg.NewFemuGrpcClient(config)
	if err != nil {
		return nil, err
	}

	return &client, nil
}

func main() {
	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(&recordAudioCmd{}, "")
	subcommands.Register(&recordScreenCmd{}, "")
	subcommands.Register(&keyboardCmd{}, "")
	flag.Parse()

	client, err := newClient()
	if err != nil {
		panic(fmt.Sprintf("error while creating gRPC client: %v", err))
	}
	ctx := context.WithValue(context.Background(), "client", client)
	os.Exit(int(subcommands.Execute(ctx)))
}
