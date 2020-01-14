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
	"fmt"
	"net"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/netutil"

	"golang.org/x/crypto/ssh"
)

const (
	// Default SSH server port.
	SSHPort = 22

	// Default RSA key size.
	RSAKeySize = 2048

	// The allowed timeout to establish an SSH connection.
	connTimeout = 5 * time.Second
	// The total allowed timeout to establish an SSH connection and complete an auth handshake.
	totalDialTimeout = 10 * time.Second

	sshUser = "fuchsia"

	// A conventionally used global request name for checking the status of a client
	// connection to an OpenSSH server.
	keepAliveOpenSSH = "keepalive@openssh.com"
)

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

// ConnectionError is an all-purpose error indicating the a client had become unresponsive.
type ConnectionError struct {
	Err error
}

func (e *ConnectionError) Error() string {
	return e.Err.Error()
}

func (e *ConnectionError) Unwrap() error {
	return e.Err
}

// IsConnectionError returns whether an error is of type *ConnectionError.
// TODO(fxbug.dev/43518): replace usage with errors.Is when on go v1.13.
func IsConnectionError(err error) bool {
	_, ok := err.(*ConnectionError)
	return ok
}

// CheckConnection returns nil if a connection is verified as still alive; else
// it returns a *ConnectionError.
func CheckConnection(client *ssh.Client) error {
	if _, _, err := client.Conn.SendRequest(keepAliveOpenSSH, true, nil); err != nil {
		return &ConnectionError{err}
	}
	return nil
}

// Connect establishes an SSH connection at the given remote address.
func Connect(ctx context.Context, raddr net.Addr, config *ssh.ClientConfig) (*ssh.Client, error) {
	network, err := network(raddr)
	if err != nil {
		return nil, err
	}

	var client *ssh.Client
	// TODO: figure out optimal backoff time and number of retries
	if err := retry.Retry(ctx, retry.WithMaxDuration(&retry.ZeroBackoff{}, time.Minute), func() error {
		var err error
		client, err = dialWithTimeout(network, raddr.String(), config, totalDialTimeout)
		return err
	}, nil); err != nil {
		return nil, fmt.Errorf("cannot connect to address %q: %v", raddr, err)
	}

	return client, nil
}

// ssh.Dial can hang during authentication, the 'timeout' being set in the config only
// applying to establishment of the initial connection. This function is effectively
// ssh.Dial with the ability to set a deadline on the underlying connection.
//
// See https://github.com/golang/go/issues/21941 for more details on the hang.
func dialWithTimeout(network, addr string, config *ssh.ClientConfig, timeout time.Duration) (*ssh.Client, error) {
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
	return ssh.NewClient(c, chans, reqs), nil
}

// ConnectToNode connects to the device with the given nodename.
func ConnectToNode(ctx context.Context, nodename string, config *ssh.ClientConfig) (*ssh.Client, error) {
	addr, err := netutil.GetNodeAddress(ctx, nodename, true)
	if err != nil {
		return nil, err
	}
	addr.Port = SSHPort
	return Connect(ctx, addr, config)
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
		Timeout:         connTimeout,
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
