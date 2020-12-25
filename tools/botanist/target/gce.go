// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package target

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"os/user"
	"time"

	"go.fuchsia.dev/fuchsia/tools/bootserver"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"

	"golang.org/x/crypto/ssh"
)

const (
	gcemClientBinary  = "./gcem_client"
	gceSerialEndpoint = "ssh-serialport.googleapis.com:9600"
)

// gceSerial is a ReadWriteCloser that talks to a GCE serial port via SSH.
type gceSerial struct {
	in     io.WriteCloser
	out    io.Reader
	sess   *ssh.Session
	client *ssh.Client
}

func newGCESerial(pkeyPath, username, endpoint string) (*gceSerial, error) {
	// Load the pkey and use it to dial the GCE serial port.
	data, err := ioutil.ReadFile(pkeyPath)
	if err != nil {
		return nil, err
	}
	signer, err := ssh.ParsePrivateKey(data)
	if err != nil {
		return nil, err
	}
	sshConfig := &ssh.ClientConfig{
		User: username,
		Auth: []ssh.AuthMethod{
			ssh.PublicKeys(signer),
		},
		// TODO(rudymathu): Replace this with google ssh serial port key.
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
	}
	client, err := ssh.Dial("tcp", endpoint, sshConfig)
	if err != nil {
		return nil, err
	}

	// Create an SSH shell and wire up stdio.
	session, err := client.NewSession()
	if err != nil {
		return nil, err
	}
	out, err := session.StdoutPipe()
	if err != nil {
		return nil, err
	}
	in, err := session.StdinPipe()
	if err != nil {
		return nil, err
	}
	if err := session.Shell(); err != nil {
		return nil, err
	}
	return &gceSerial{
		in:     in,
		out:    out,
		sess:   session,
		client: client,
	}, nil
}

func (s *gceSerial) Read(b []byte) (int, error) {
	return s.out.Read(b)
}

func (s *gceSerial) Write(b []byte) (int, error) {
	return s.in.Write(b)
}

func (s *gceSerial) Close() error {
	multierr := ""
	if err := s.in.Close(); err != nil {
		multierr += fmt.Sprintf("failed to close serial SSH session input pipe: %s, ", err)
	}
	if err := s.sess.Close(); err != nil {
		multierr += fmt.Sprintf("failed to close serial SSH session: %s, ", err)
	}
	if err := s.client.Close(); err != nil {
		multierr += fmt.Sprintf("failed to close serial SSH client: %s", err)
	}
	if multierr != "" {
		return errors.New(multierr)
	}
	return nil
}

// GCEConfig represents the on disk config used by botanist to launch a GCE
// instance.
type GCEConfig struct {
	// MediatorURL is the url of the GCE Mediator.
	MediatorURL string `json:"mediator_url"`
	// BuildID is the swarming task ID of the associated build.
	BuildID string `json:"build_id"`
	// CloudProject is the cloud project to create the GCE Instance in.
	CloudProject string `json:"cloud_project"`
	// SwarmingServer is the URL to the swarming server that fed us this
	// task.
	SwarmingServer string `json:"swarming_server"`
	// MachineShape is the shape of the instance we want to create.
	MachineShape string `json:"machine_shape"`
}

// GCETarget represents a GCE VM running Fuchsia.
type GCETarget struct {
	config       GCEConfig
	currentUser  string
	instanceName string
	loggerCtx    context.Context
	opts         Options
	pubkeyPath   string
	serial       io.ReadWriteCloser
	zone         string
}

// createInstanceRes is returned by the gcem_client's create-instance
// subcommand. Its schema is determined by the CreateInstanceRes proto
// message in http://google3/turquoise/infra/gce_mediator/proto/mediator.proto.
type createInstanceRes struct {
	InstanceName string `json:"instanceName"`
	Zone         string `json:"zone"`
}

// NewGCETarget creates, starts, and connects to the serial console of a GCE VM.
func NewGCETarget(ctx context.Context, config GCEConfig, opts Options) (*GCETarget, error) {
	// Generate an SSH keypair. We do this even if the caller has provided
	// an SSH key in opts because we require a very specific input format:
	// PEM encoded, PKCS1 marshaled RSA keys.
	pkeyPath, err := generatePrivateKey()
	if err != nil {
		return nil, err
	}
	opts.SSHKey = pkeyPath
	pubkeyPath, err := generatePublicKey(opts.SSHKey)
	if err != nil {
		return nil, err
	}
	logger.Infof(ctx, "generated SSH key pair for use with GCE instance")

	u, err := user.Current()
	if err != nil {
		return nil, err
	}
	g := &GCETarget{
		config:      config,
		currentUser: u.Username,
		loggerCtx:   ctx,
		opts:        opts,
		pubkeyPath:  pubkeyPath,
	}

	// Set up and execute the command to create the instance.
	logger.Infof(ctx, "creating the GCE instance")
	expBackoff := retry.NewExponentialBackoff(15*time.Second, 2*time.Minute, 2)
	createInstanceErrs := make(chan error)
	defer close(createInstanceErrs)
	go logErrors(ctx, "createInstance()", createInstanceErrs)
	if err := retry.Retry(ctx, expBackoff, g.createInstance, createInstanceErrs); err != nil {
		return nil, err
	}

	// Connect to the serial line.
	logger.Infof(ctx, "setting up the serial connection to the GCE instance")
	expBackoff = retry.NewExponentialBackoff(15*time.Second, 2*time.Minute, 2)
	connectSerialErrs := make(chan error)
	defer close(connectSerialErrs)
	go logErrors(ctx, "connectToSerial()", connectSerialErrs)
	if err := retry.Retry(ctx, expBackoff, g.connectToSerial, connectSerialErrs); err != nil {
		return nil, err
	}
	return g, nil
}

func logErrors(ctx context.Context, functionName string, errs <-chan error) {
	for {
		err, more := <-errs
		if err != nil {
			logger.Errorf(ctx, "%s failed: %s, retrying", functionName, err)
		}
		if !more {
			return
		}
	}
}

func (g *GCETarget) connectToSerial() error {
	username := fmt.Sprintf(
		"%s.%s.%s.%s",
		g.config.CloudProject,
		g.zone,
		g.instanceName,
		g.currentUser,
	)
	serial, err := newGCESerial(g.opts.SSHKey, username, gceSerialEndpoint)
	g.serial = serial
	return err
}

func (g *GCETarget) createInstance() error {
	taskID := os.Getenv("SWARMING_TASK_ID")
	if taskID == "" {
		return errors.New("task did not specify SWARMING_TASK_ID")
	}

	invocation := []string{
		gcemClientBinary,
		"create-instance",
		"-host", g.config.MediatorURL,
		"-project", g.config.CloudProject,
		"-build-id", g.config.BuildID,
		"-task-id", taskID,
		"-swarming-host", g.config.SwarmingServer,
		"-machine-shape", g.config.MachineShape,
		"-user", g.currentUser,
		"-pubkey", g.pubkeyPath,
	}

	logger.Infof(g.loggerCtx, "GCE Mediator client command: %v", invocation)
	cmd := exec.Command(invocation[0], invocation[1:]...)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return err
	}
	cmd.Stderr = os.Stderr

	if err := cmd.Start(); err != nil {
		return err
	}
	var res createInstanceRes
	if err := json.NewDecoder(stdout).Decode(&res); err != nil {
		return err
	}
	if err := cmd.Wait(); err != nil {
		return err
	}
	g.instanceName = res.InstanceName
	g.zone = res.Zone
	return nil
}

// generatePrivateKey generates a 2048 bit RSA private key, writes it to
// a temporary file, and returns the path to the key.
func generatePrivateKey() (string, error) {
	pkey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return "", err
	}
	f, err := ioutil.TempFile("", "gce_pkey")
	if err != nil {
		return "", err
	}
	defer f.Close()
	pemBlock := &pem.Block{
		Type:    "RSA PRIVATE KEY",
		Headers: nil,
		Bytes:   x509.MarshalPKCS1PrivateKey(pkey),
	}
	return f.Name(), pem.Encode(f, pemBlock)
}

// generatePublicKey reads the private key at path pkey and generates a public
// key in Authorized Keys format. Returns the path to the public key file.
func generatePublicKey(pkeyFile string) (string, error) {
	if pkeyFile == "" {
		return "", errors.New("no private key file provided")
	}
	data, err := ioutil.ReadFile(pkeyFile)
	if err != nil {
		return "", err
	}
	block, _ := pem.Decode(data)
	pkey, err := x509.ParsePKCS1PrivateKey(block.Bytes)
	if err != nil {
		return "", err
	}
	pubkey, err := ssh.NewPublicKey(pkey.Public())
	if err != nil {
		return "", err
	}
	f, err := ioutil.TempFile("", "gce_pubkey")
	if err != nil {
		return "", err
	}
	defer f.Close()
	_, err = f.Write(ssh.MarshalAuthorizedKey(pubkey))
	return f.Name(), err
}

func (g *GCETarget) Nodename() string {
	// TODO(rudymathu): fill in nodename
	return ""
}

func (g *GCETarget) Serial() io.ReadWriteCloser {
	return g.serial
}

func (g *GCETarget) SSHKey() string {
	return g.opts.SSHKey
}

func (g *GCETarget) Start(ctx context.Context, _ []bootserver.Image, args []string, _ string) error {
	return nil
}

func (g *GCETarget) Stop(context.Context) error {
	return g.serial.Close()
}

func (g *GCETarget) Wait(context.Context) error {
	return ErrUnimplemented
}
