// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package resultstore

import (
	"context"
	"crypto/x509"
	"flag"
	"fmt"
	"log"

	"github.com/google/uuid"
	"go.chromium.org/luci/auth"
	"go.chromium.org/luci/auth/client/authcli"
	"go.chromium.org/luci/hardcoded/chromeinfra"
	api "google.golang.org/genproto/googleapis/devtools/resultstore/v2"
	"google.golang.org/genproto/protobuf/field_mask"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
)

// Returns the list of Google Cloud API scopes required to use the ResultStore Upload API.
func requiredScopes() []string {
	return []string{"https://www.googleapis.com/auth/cloud-platform"}
}

// UploadClientFlags are used to parse commnad-line options for creating a new ResultStore
// UploadClient. The options are, in turn, used to generate UploadClientOptions. Example:
//
//  var cf UploadClientFlags
//
//  func init() {
//    cf.Register(flag.CommandLine)
//  }
//
//  func Main(ctx context.Background()) {
//    flag.Parse()
//    opts, err := cf.Options(ctx)
//    // check err ...
//    client, err := NewClient(ctx, opts)
//    // check err ...
//  }
type UploadClientFlags struct {
	authFlags authcli.Flags
	environ   Environment
}

// Register sets these UploadClient flags on the given flag.FlagSet.
func (f *UploadClientFlags) Register(in *flag.FlagSet) {
	// LUCI auth flags
	defaultAuthOpts := chromeinfra.DefaultAuthOptions()
	defaultAuthOpts.Scopes = append(defaultAuthOpts.Scopes, requiredScopes()...)
	f.authFlags.Register(in, defaultAuthOpts)

	// ResultStore flags.
	environs := []Environment{Production, Staging}
	in.Var(&f.environ, "environment", fmt.Sprintf("ResultStore environment: %v", environs))
}

// Options returns UploadClientOptions created from this UploadClientFlags' inputs.
func (f *UploadClientFlags) Options() (*UploadClientOptions, error) {
	authOpts, err := f.authFlags.Options()
	if err != nil {
		return nil, fmt.Errorf("failed to create LUCI auth options: %v", err)
	}
	return &UploadClientOptions{
		Environ:  f.environ,
		AuthOpts: authOpts,
	}, nil
}

// UploadClientOptions are used to create new UploadClients. See UploadClientFlags for
// example usage.
type UploadClientOptions struct {
	Environ  Environment
	AuthOpts auth.Options
}

// NewClient returns a new UploadClient connected to a ResultStore backend.
func NewClient(ctx context.Context, opts UploadClientOptions) (*UploadClient, error) {
	// Generate transport credentials.
	pool, err := x509.SystemCertPool()
	if err != nil {
		return nil, fmt.Errorf("failed to create cert pool: %v", err)
	}
	tcreds, err := credentials.NewClientTLSFromCert(pool, ""), nil
	if err != nil {
		return nil, err
	}
	// Generate per RPC credentials.
	authenticator := auth.NewAuthenticator(ctx, auth.SilentLogin, opts.AuthOpts)
	pcreds, err := authenticator.PerRPCCredentials()
	if err != nil {
		return nil, err
	}
	conn, err := grpc.Dial(
		opts.Environ.GRPCServiceAddress(),
		grpc.WithTransportCredentials(tcreds),
		grpc.WithPerRPCCredentials(pcreds),
	)
	if err != nil {
		return nil, err
	}
	return &UploadClient{
		client: api.NewResultStoreUploadClient(conn),
		conn:   conn,
	}, nil
}

// UploadClient wraps the ResultStoreUpload client libraries.
//
// UploadClient generates UUIDs for each request sent to Resultstore. If the provided
// Context object contains a non-empty string value for TestUUIDKey, that value is
// used instead. This is done by calling `SetTestUUID(ctx, "uuid")`.
//
// UploadClient requires an Invocation's authorization token to be set in the provided
// Context. This can be done by calling: `SetAuthToken(ctx, "auth-token")`.
//
// The user should Close() the client when finished.
type UploadClient struct {
	client api.ResultStoreUploadClient
	conn   *grpc.ClientConn
}

// Close closes this UploadClient's connection to ResultStore.
func (c *UploadClient) Close() error {
	return c.conn.Close()
}

// CreateInvocation creates an Invocation in ResultStore. This must be called before
// any other "Create" methods, since all resources belong to an Invocation.
func (c *UploadClient) CreateInvocation(ctx context.Context, invocation *Invocation) (*Invocation, error) {
	authToken, err := AuthToken(ctx)
	if err != nil {
		return nil, err
	}

	res, err := c.client.CreateInvocation(ctx, &api.CreateInvocationRequest{
		RequestId:          c.uuid(ctx),
		AuthorizationToken: authToken,
		InvocationId:       invocation.ID,
		Invocation:         invocation.ToResultStoreInvocation(),
	})
	if err != nil {
		return nil, err
	}

	output := new(Invocation)
	output.FromResultStoreInvocation(res)
	return output, nil
}

// CreateConfiguration creates a new Configuration. Configurations typically represent
// build or test environments.
func (c *UploadClient) CreateConfiguration(ctx context.Context, config *Configuration, invocationName string) (*Configuration, error) {
	authToken, err := AuthToken(ctx)
	if err != nil {
		return nil, err
	}

	res, err := c.client.CreateConfiguration(ctx, &api.CreateConfigurationRequest{
		RequestId:          c.uuid(ctx),
		AuthorizationToken: authToken,
		Parent:             invocationName,
		ConfigId:           config.ID,
		Configuration:      config.ToResultStoreConfiguration(),
	})
	if err != nil {
		return nil, err
	}

	output := new(Configuration)
	output.FromResultStoreConfiguration(res)
	return output, nil
}

// CreateTarget creates a new build or test target for the Invocation with the given
// name.
func (c *UploadClient) CreateTarget(ctx context.Context, target *Target, invocationName string) (*Target, error) {
	authToken, err := AuthToken(ctx)
	if err != nil {
		return nil, err
	}

	res, err := c.client.CreateTarget(ctx, &api.CreateTargetRequest{
		RequestId:          c.uuid(ctx),
		AuthorizationToken: authToken,
		Parent:             invocationName,
		TargetId:           target.ID.ID,
		Target:             target.ToResultStoreTarget(),
	})
	if err != nil {
		return nil, err
	}

	output := new(Target)
	output.FromResultStoreTarget(res)
	return output, nil
}

// CreateConfiguredTarget creates a new ConfiguredTarget for the Target with the given
// name. ConfiguredTargets represent targets executing with a specific Configuration.
func (c *UploadClient) CreateConfiguredTarget(ctx context.Context, configTarget *ConfiguredTarget, targetName string) (*ConfiguredTarget, error) {
	authToken, err := AuthToken(ctx)
	if err != nil {
		return nil, err
	}

	res, err := c.client.CreateConfiguredTarget(ctx, &api.CreateConfiguredTargetRequest{
		AuthorizationToken: authToken,
		RequestId:          c.uuid(ctx),
		Parent:             targetName,
		ConfigId:           configTarget.ID.ConfigID,
		ConfiguredTarget:   configTarget.ToResultStoreConfiguredTarget(),
	})
	if err != nil {
		return nil, err
	}

	output := new(ConfiguredTarget)
	output.FromResultStoreConfiguredTarget(res)
	return output, nil
}

// CreateTestAction creates a new Action for the ConfiguredTarget with the given name.
func (c *UploadClient) CreateTestAction(ctx context.Context, action *TestAction, configTargetName string) (*TestAction, error) {
	authToken, err := AuthToken(ctx)
	if err != nil {
		return nil, err
	}

	res, err := c.client.CreateAction(ctx, &api.CreateActionRequest{
		AuthorizationToken: authToken,
		RequestId:          c.uuid(ctx),
		Parent:             configTargetName,
		ActionId:           action.ID.ID,
		Action:             action.ToResultStoreAction(),
	})
	if err != nil {
		return nil, err
	}

	output := new(TestAction)
	output.FromResultStoreAction(res)
	return output, nil
}

// UpdateTestAction updates a list of TestAction properties (proto field paths) to the
// values specified in the given TestAction.  Fields that match the mask but aren't
// populated in the given TestAction are cleared.
func (c *UploadClient) UpdateTestAction(ctx context.Context, action *TestAction, fields []string) (*TestAction, error) {
	authToken, err := AuthToken(ctx)
	if err != nil {
		return nil, err
	}

	res, err := c.client.UpdateAction(ctx, &api.UpdateActionRequest{
		AuthorizationToken: authToken,
		Action:             action.ToResultStoreAction(),
		UpdateMask:         &field_mask.FieldMask{Paths: fields},
	})
	if err != nil {
		return nil, err
	}

	output := new(TestAction)
	output.FromResultStoreAction(res)
	return output, nil
}

// UpdateConfiguredTarget updates a list of ConfiguredTarget properties (proto field
// paths) to the values specified in the given ConfiguredTarget.  Fields that match
// the mask but aren't populated in the given ConfiguredTarget are cleared.
func (c *UploadClient) UpdateConfiguredTarget(ctx context.Context, configTarget *ConfiguredTarget, fields []string) (*ConfiguredTarget, error) {
	authToken, err := AuthToken(ctx)
	if err != nil {
		return nil, err
	}

	res, err := c.client.UpdateConfiguredTarget(ctx, &api.UpdateConfiguredTargetRequest{
		AuthorizationToken: authToken,
		ConfiguredTarget:   configTarget.ToResultStoreConfiguredTarget(),
		UpdateMask:         &field_mask.FieldMask{Paths: fields},
	})
	if err != nil {
		return nil, err
	}

	output := new(ConfiguredTarget)
	output.FromResultStoreConfiguredTarget(res)
	return output, nil
}

// UpdateTarget updates a list of Target properties (proto field paths) to the values
// specified in the given Target.  Fields that match the mask but aren't populated in
// the given Target are cleared.
func (c *UploadClient) UpdateTarget(ctx context.Context, target *Target, fields []string) (*Target, error) {
	authToken, err := AuthToken(ctx)
	if err != nil {
		return nil, err
	}

	res, err := c.client.UpdateTarget(ctx, &api.UpdateTargetRequest{
		AuthorizationToken: authToken,
		UpdateMask:         &field_mask.FieldMask{Paths: fields},
		Target:             target.ToResultStoreTarget(),
	})
	if err != nil {
		return nil, err
	}

	output := new(Target)
	output.FromResultStoreTarget(res)
	return output, nil
}

// UpdateInvocation updates a list of Invocation properties (proto field paths) to the
// values specified in the given Invocation.  Fields that match the mask but aren't
// populated in the given Invocation are cleared.
func (c *UploadClient) UpdateInvocation(ctx context.Context, invocation *Invocation, fields []string) (*Invocation, error) {
	authToken, err := AuthToken(ctx)
	if err != nil {
		return nil, err
	}

	res, err := c.client.UpdateInvocation(ctx, &api.UpdateInvocationRequest{
		AuthorizationToken: authToken,
		UpdateMask:         &field_mask.FieldMask{Paths: fields},
		Invocation:         invocation.ToResultStoreInvocation(),
	})
	if err != nil {
		return nil, err
	}

	output := new(Invocation)
	output.FromResultStoreInvocation(res)
	return output, nil
}

// FinishConfiguredTarget closes a ConfiguredTarget for editing.
func (c *UploadClient) FinishConfiguredTarget(ctx context.Context, name string) error {
	authToken, err := AuthToken(ctx)
	if err != nil {
		return err
	}

	_, err = c.client.FinishConfiguredTarget(ctx, &api.FinishConfiguredTargetRequest{
		AuthorizationToken: authToken,
		Name:               name,
	})
	return err
}

// FinishTarget closes a Target for editing.
func (c *UploadClient) FinishTarget(ctx context.Context, name string) error {
	authToken, err := AuthToken(ctx)
	if err != nil {
		return err
	}

	_, err = c.client.FinishTarget(ctx, &api.FinishTargetRequest{
		AuthorizationToken: authToken,
		Name:               name,
	})
	return err
}

// FinishInvocation closes an Invocation for editing.
func (c *UploadClient) FinishInvocation(ctx context.Context, name string) error {
	authToken, err := AuthToken(ctx)
	if err != nil {
		return err
	}

	_, err = c.client.FinishInvocation(ctx, &api.FinishInvocationRequest{
		AuthorizationToken: authToken,
		Name:               name,
	})
	return err
}

// uuid generates an RFC-4122 compliant Version 4 UUID.  If the provided Context contains
// a non-empty string value for TestUUIDKey, that value will be used instead. If there was
// an error when reading the UUID from the context, a message describing the error is
// logged.
func (c *UploadClient) uuid(ctx context.Context) string {
	value, err := TestUUID(ctx)

	if err != nil {
		log.Println(err)
		return uuid.New().String()
	}

	if value == "" {
		return uuid.New().String()
	}

	return value
}
