// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	"errors"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/retry"

	"golang.org/x/crypto/ssh"
)

const (
	// Default SSH server port.
	SSHPort = 22

	// Default RSA key size.
	RSAKeySize = 2048

	// The allowed timeout for a single attempt at establishing an SSH
	// connection.
	connectAttemptTimeout = 10 * time.Second

	// The allowed timeout to establish an ssh connection, possibly including
	// many attempts.
	totalConnectTimeout = 2 * time.Minute

	sshUser = "fuchsia"
)

// DefaultConnectBackoff is a sensible default for SSH clients.
func DefaultConnectBackoff() retry.Backoff {
	// NOTE: This retry strategy was somewhat arbitrarily chosen and can be
	// changed if there's a compelling reason to choose a different strategy.
	return retry.WithMaxDuration(&retry.ZeroBackoff{}, totalConnectTimeout)
}

// ConnectionError is an all-purpose error indicating that a client has become
// unresponsive.
type ConnectionError struct {
	Err error
}

func (e ConnectionError) Unwrap() error {
	return e.Err
}

func (e ConnectionError) Error() string {
	// ConnectionError is intended to be an umbrella error type for all kinds of
	// SSH-related errors, so there's no information we can add to the
	// underlying error message that would be particularly useful in all
	// scenarios.
	if e.Err != nil {
		return e.Err.Error()
	}
	return "SSH connection error"
}

// IsConnectionError determines whether the given error is a ConnectionError.
// This is a common check that we include in sshutil to save callers a line of
// code.
func IsConnectionError(err error) bool {
	var connErr ConnectionError
	return errors.As(err, &connErr)
}

// GeneratePrivateKey generates a private SSH key.
func GeneratePrivateKey() ([]byte, error) {
	key, err := rsa.GenerateKey(rand.Reader, RSAKeySize)
	if err != nil {
		return nil, err
	}
	privateKey := &pem.Block{
		Type:  "RSA PRIVATE KEY",
		Bytes: x509.MarshalPKCS1PrivateKey(key),
	}
	buf := pem.EncodeToMemory(privateKey)

	return buf, nil
}

// DefaultSSHConfig returns a basic SSH client configuration.
func DefaultSSHConfig(privateKey []byte) (*ssh.ClientConfig, error) {
	signer, err := ssh.ParsePrivateKey(privateKey)
	if err != nil {
		return nil, err
	}
	return DefaultSSHConfigFromSigners(signer)
}

// DefaultSSHConfigFromSigners returns a basic SSH client configuration.
func DefaultSSHConfigFromSigners(signers ...ssh.Signer) (*ssh.ClientConfig, error) {
	return &ssh.ClientConfig{
		User:            sshUser,
		Auth:            []ssh.AuthMethod{ssh.PublicKeys(signers...)},
		Timeout:         connectAttemptTimeout,
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
	}, nil
}
