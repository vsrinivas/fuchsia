// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	"errors"
	"fmt"
	"net"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/netutil"

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

var (
	// defaultConnectBackoff is the connection backoff for clients returned by
	// Connect() and ConnectToNode().
	defaultConnectBackoff = retry.WithMaxDuration(&retry.ZeroBackoff{}, totalConnectTimeout)
)

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

// CheckConnection returns nil if a connection is verified as still alive; else
// it returns an error that unwraps as a ConnectionError.
func CheckConnection(client *ssh.Client) error {
	if _, _, err := client.Conn.SendRequest(keepaliveOpenSSH, true, nil); err != nil {
		return ConnectionError{err}
	}
	return nil
}

// ConnectDeprecated establishes an SSH connection at the given remote address.
// If it fails to connect, it will return an error that unwraps as a
// ConnectionError.
// TODO(fxb/48042): Delete in favor of a method that returns sshutil.Client.
func ConnectDeprecated(ctx context.Context, raddr net.Addr, config *ssh.ClientConfig) (*ssh.Client, error) {
	network, err := network(raddr)
	if err != nil {
		return nil, err
	}

	var client *ssh.Client
	// TODO: figure out optimal backoff time and number of retries
	startTime := time.Now()
	if err := retry.Retry(ctx, defaultConnectBackoff, func() error {
		var err error
		client, err = dialWithTimeout(ctx, network, raddr.String(), config, connectAttemptTimeout)
		return err
	}, nil); err != nil {
		var netErr net.Error
		if errors.As(err, &netErr) && netErr.Timeout() {
			// The exact time at which the timeout triggers is nondeterministic;
			// it'll be somewhere between `totalConnectTimeout` and
			// `totalConnectTimeout + connectAttemptTimeout`. So we measure the
			// duration to improve accuracy.
			duration := time.Now().Sub(startTime).Truncate(time.Second)
			err = ConnectionError{fmt.Errorf("timed out trying to connect to ssh after %v", duration)}
		} else {
			err = ConnectionError{fmt.Errorf("cannot connect to address %q: %v", raddr, err)}
		}
		return nil, err
	}

	return client, nil
}

// ssh.Dial can hang during authentication, the 'timeout' being set in the config only
// applying to establishment of the initial connection. This function is effectively
// ssh.Dial with the ability to set a deadline on the underlying connection.
//
// See https://github.com/golang/go/issues/21941 for more details on the hang.
func dialWithTimeout(ctx context.Context, network, addr string, config *ssh.ClientConfig, timeout time.Duration) (*ssh.Client, error) {
	conn, err := net.DialTimeout(network, addr, config.Timeout)
	if err != nil {
		return nil, err
	}
	if err := conn.SetDeadline(time.Now().Add(timeout)); err != nil {
		conn.Close()
		return nil, err
	}
	c, chans, reqs, err := ssh.NewClientConn(conn, addr, config)
	if err != nil {
		conn.Close()
		return nil, err
	}
	if err := conn.SetDeadline(time.Time{}); err != nil {
		c.Close()
		return nil, err
	}
	client := ssh.NewClient(c, chans, reqs)
	go keepalive(ctx, conn, client)
	return client, nil
}

// ConnectToNodeConnectToNodeDeprecated connects to the device with the given
// nodename.
// TODO(fxb/48042): Delete in favor of a method that returns sshutil.Client.
func ConnectToNodeDeprecated(ctx context.Context, nodename string, config *ssh.ClientConfig) (*ssh.Client, error) {
	addr, err := netutil.GetNodeAddress(ctx, nodename, true)
	if err != nil {
		return nil, err
	}
	addr.Port = SSHPort
	return ConnectDeprecated(ctx, addr, config)
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

// Returns the network to use to SSH into a device.
func network(address net.Addr) (string, error) {
	var ip *net.IP

	// We need these type assertions because the net package (annoyingly) doesn't provide
	// an interface for objects that have an IP address.
	switch addr := address.(type) {
	case *net.UDPAddr:
		ip = &addr.IP
	case *net.TCPAddr:
		ip = &addr.IP
	case *net.IPAddr:
		ip = &addr.IP
	default:
		return "", fmt.Errorf("unsupported address type: %T", address)
	}

	if ip.To4() != nil {
		return "tcp", nil // IPv4
	}
	if ip.To16() != nil {
		return "tcp6", nil // IPv6
	}
	return "", fmt.Errorf("cannot infer network for IP address %s", ip.String())
}

// keepalive runs for the duration of the client's lifetime, sending periodic
// pings (with response timeouts) to ensure that the client is still connected.
// If a ping fails, it will close the client and exit.
func keepalive(ctx context.Context, conn net.Conn, client *ssh.Client) {
	ticker := time.NewTicker(defaultKeepaliveInterval)
	defer ticker.Stop()
	for range ticker.C {
		if err := emitKeepalive(conn, client); err != nil {
			// Try to close the client. It's possible the keepalive failed
			// because the client has already been closed, which is fine – this
			// close will just silently fail.
			logger.Errorf(ctx, "ssh keepalive failed, closing client: %v", err)
			client.Close()
			return
		}
	}
}

func emitKeepalive(conn net.Conn, client *ssh.Client) error {
	conn.SetDeadline(time.Now().Add(defaultKeepaliveTimeout))
	return CheckConnection(client)
}
