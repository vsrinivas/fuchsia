// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package resultstore

import (
	"context"
	"crypto/x509"
	"fmt"

	api "google.golang.org/genproto/googleapis/devtools/resultstore/v2"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
)

var (
	// Google Cloud API scope required to use ResultStore Upload API.
	RequiredScopes = []string{
		"https://www.googleapis.com/auth/cloud-platform",
	}
)

// Connect returns a new UploadClient connected to the ResultStore backend at the given host.
func Connect(ctx context.Context, environment Environment, creds credentials.PerRPCCredentials) (*UploadClient, error) {
	pool, err := x509.SystemCertPool()
	if err != nil {
		return nil, fmt.Errorf("failed to create cert pool: %v", err)
	}

	transportCreds := credentials.NewClientTLSFromCert(pool, "")

	conn, err := grpc.Dial(
		environment.GRPCServiceAddress(),
		grpc.WithTransportCredentials(transportCreds),
		grpc.WithPerRPCCredentials(creds),
	)
	if err != nil {
		return nil, err
	}
	return NewUploadClient(api.NewResultStoreUploadClient(conn)), nil
}

// Environment describes which ResultStore environment to use.
type Environment string

// Environment constants.
const (
	Production Environment = "production"
	Staging    Environment = "staging"
)

// Returns the URL of the Invocation with the given ID in this Environment.
func (e Environment) InvocationURL(invocationID string) string {
	return fmt.Sprintf("https://%s/results/invocations/%s/targets", e.frontendHostname(), invocationID)
}

// FrontendHostname is the hostname of the ResultStore UI for this Environment.
func (e Environment) frontendHostname() string {
	switch e {
	case Production:
		return "source.cloud.google.com"
	case Staging:
		return "grimoire-stagingprod.corp.google.com"
	default:
		// We should never get here.
		panic("invalid environment: " + e)
	}
}

// GRPCServiceAddress is the address of the ResultStoreUpload gRPC service.
func (e Environment) GRPCServiceAddress() string {
	switch e {
	case Production:
		return "resultstore.googleapis.com:443"
	case Staging:
		return "staging-resultstore.googleapis.com:443"
	default:
		// We should never get here.
		panic("invalid environment: " + e)
	}
}

func (e Environment) String() string {
	return string(e)
}

// Set implements flag.Var
func (e *Environment) Set(value string) error {
	switch value {
	case string(Production), string(Staging):
		*e = Environment(value)
	default:
		return fmt.Errorf("invalid environment: %q", value)
	}

	return nil
}
