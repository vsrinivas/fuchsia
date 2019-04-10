// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package resultstore

import "fmt"

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
