// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"path"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/golang/glog"
	"github.com/kr/fs"
	"github.com/pkg/sftp"
	"golang.org/x/crypto/ssh"
)

// A Connector is used to communicate with an instance
type Connector interface {
	// Connect establishes all necessary connections to the instance. It does
	// not need to be explicitly called, because the other Connector methods will
	// automatically connect if necessary, but may be called during initializiation.
	// It the connector is already connected, an error will be returned.
	Connect() error

	// Close closes any open connections to the instance. It is the client's
	// responsibility to call Close() when cleaning up the Connector.
	Close()

	// Returns an InstanceCmd representing the command to be run on the instance. Only one
	// command should be active at a time.
	// TODO(fxbug.dev/47479): In some cases, we should be able to relax the above restriction
	Command(name string, args ...string) InstanceCmd

	// Copies targetSrc (may include globs) to hostDst, which is always assumed
	// to be a directory. Directories are copied recursively.
	Get(targetSrc, hostDst string) error

	// Copies hostSrc (may include globs) to targetDst, which is always assumed
	// to be a directory. Directories are copied recursively.
	Put(hostSrc, targetDst string) error

	// Retrieves a syslog from the instance, filtered to the given process ID
	GetSysLog(pid int) (string, error)
}

// An SSHConnector is a Connector that uses SSH/SFTP for transport
// Note: exported fields will be serialized to the handle
type SSHConnector struct {
	// Host can be any IP or hostname as accepted by net.Dial
	Host string
	Port int
	// Key is a path to the SSH private key that should be used for
	// authentication
	Key string

	client     *ssh.Client
	sftpClient *sftp.Client
}

// Connect to the remote server
func (c *SSHConnector) Connect() error {
	if c.client != nil {
		return fmt.Errorf("Connect called, but already connected")
	}

	glog.Info("SSH: connecting...")
	key, err := ioutil.ReadFile(c.Key)
	if err != nil {
		return fmt.Errorf("error reading ssh key: %s", err)
	}

	signer, err := ssh.ParsePrivateKey(key)
	if err != nil {
		return fmt.Errorf("error parsing ssh key: %s", err)
	}

	config := &ssh.ClientConfig{
		User: "clusterfuchsia",
		Auth: []ssh.AuthMethod{
			ssh.PublicKeys(signer),
		},
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
	}

	// TODO(fxbug.dev/45424): dial timeout
	address := net.JoinHostPort(c.Host, strconv.Itoa(c.Port))
	client, err := ssh.Dial("tcp", address, config)
	if err != nil {
		return fmt.Errorf("error connecting ssh: %s", err)
	}

	glog.Info("SSH: connected")
	c.client = client

	sftpClient, err := sftp.NewClient(c.client)
	if err != nil {
		return fmt.Errorf("error connecting sftp: %s", err)
	}

	glog.Info("SFTP: connected")
	c.sftpClient = sftpClient

	return nil
}

// Close any open connections
func (c *SSHConnector) Close() {
	glog.Info("Closing SSH/SFTP")

	// TODO(fxbug.dev/47316): Look into errors thrown by these Closes when
	// disconnecting from in-memory SSH server
	if c.client != nil {
		if err := c.client.Close(); err != nil {
			glog.Warningf("Error while closing SSH: %s", err)
		}
		c.client = nil
	}

	if c.sftpClient != nil {
		if err := c.sftpClient.Close(); err != nil {
			glog.Warningf("Error while closing SFTP: %s", err)
		}
		c.sftpClient = nil
	}
}

// Command returns an InstanceCmd that can be used to given command over SSH
func (c *SSHConnector) Command(name string, args ...string) InstanceCmd {
	// TODO(fxbug.dev/45424): Would be best to shell escape
	cmdline := strings.Join(append([]string{name}, args...), " ")
	return &SSHInstanceCmd{connector: c, cmdline: cmdline}
}

// GetSysLog will fetch the syslog by running a remote command
func (c *SSHConnector) GetSysLog(pid int) (string, error) {
	cmd := c.Command("log_listener", "--dump_logs", "yes", "--pretty", "no",
		"--pid", strconv.Itoa(pid))

	out, err := cmd.Output()
	if err != nil {
		return "", err
	}
	return string(out), nil
}

// Get fetches files over SFTP
func (c *SSHConnector) Get(targetSrc string, hostDst string) error {
	if c.sftpClient == nil {
		if err := c.Connect(); err != nil {
			return err
		}
	}

	// Expand any globs in source path
	srcList, err := c.sftpClient.Glob(targetSrc)
	if err != nil {
		return fmt.Errorf("error during glob expansion: %s", err)
	}
	if len(srcList) == 0 {
		return fmt.Errorf("no files matching glob: '%s'", targetSrc)
	}

	for _, root := range srcList {
		walker := c.sftpClient.Walk(root)
		for walker.Step() {
			if err := walker.Err(); err != nil {
				return fmt.Errorf("error while walking %q: %s", root, err)
			}

			src := walker.Path()
			relPath, err := filepath.Rel(filepath.Dir(root), src)
			if err != nil {
				return fmt.Errorf("error taking relpath for %q: %s", src, err)
			}
			dst := path.Join(hostDst, relPath)

			// Create local directory if necessary
			if walker.Stat().IsDir() {
				if _, err := os.Stat(dst); os.IsNotExist(err) {
					os.Mkdir(dst, os.ModeDir|0755)
				}
				continue
			}

			glog.Infof("Copying [remote]:%s to %s", src, dst)

			fin, err := c.sftpClient.Open(src)
			if err != nil {
				return fmt.Errorf("error opening remote file: %s", err)
			}
			defer fin.Close()

			fout, err := os.Create(dst)
			if err != nil {
				return fmt.Errorf("error creating local file: %s", err)
			}
			defer fout.Close()
			if _, err := io.Copy(fout, fin); err != nil {
				return fmt.Errorf("error copying file: %s", err)
			}

		}
	}
	return nil
}

// Put uploads files over SFTP
func (c *SSHConnector) Put(hostSrc string, targetDst string) error {
	if c.sftpClient == nil {
		if err := c.Connect(); err != nil {
			return err
		}
	}

	// Expand any globs in source path
	srcList, err := filepath.Glob(hostSrc)
	if err != nil {
		return fmt.Errorf("error during glob expansion: %s", err)
	}
	if len(srcList) == 0 {
		return fmt.Errorf("no files matching glob: '%s'", hostSrc)
	}

	for _, root := range srcList {
		walker := fs.Walk(root)
		for walker.Step() {
			if err := walker.Err(); err != nil {
				return fmt.Errorf("error while walking %q: %s", root, err)
			}

			src := walker.Path()
			relPath, err := filepath.Rel(filepath.Dir(root), src)
			if err != nil {
				return fmt.Errorf("error taking relpath for %q: %s", src, err)
			}
			// filepath.Rel converts to host OS separators, while remote is always /
			dst := path.Join(targetDst, filepath.ToSlash(relPath))

			// Create remote directory if necessary
			if walker.Stat().IsDir() {
				if _, err := c.sftpClient.Stat(dst); err == nil {
					continue
				} else if !os.IsNotExist(err) {
					return fmt.Errorf("error stat-ing remote directory %q: %s", dst, err)
				}

				if err := c.sftpClient.Mkdir(dst); err != nil {
					return fmt.Errorf("error creating remote directory %q: %s", dst, err)
				}
				continue
			}

			glog.Infof("Copying %s to [remote]:%s", src, dst)

			fin, err := os.Open(src)
			defer fin.Close()
			if err != nil {
				return fmt.Errorf("error opening local file: %s", err)
			}

			fout, err := c.sftpClient.Create(dst)
			defer fout.Close()
			if err != nil {
				return fmt.Errorf("error creating remote file: %s", err)
			}
			if _, err := io.Copy(fout, fin); err != nil {
				return fmt.Errorf("error copying file: %s", err)
			}
		}

	}
	return nil
}

func loadConnectorFromHandle(handle Handle) (Connector, error) {
	// TODO(fxbug.dev/47479): detect connector type
	var conn SSHConnector

	if err := handle.PopulateObject(&conn); err != nil {
		return nil, err
	}

	if conn.Host == "" {
		return nil, fmt.Errorf("host not found in handle")
	}
	if conn.Port == 0 {
		return nil, fmt.Errorf("port not found in handle")
	}
	if conn.Key == "" {
		return nil, fmt.Errorf("key not found in handle")
	}

	return &conn, nil
}

// Generate a key to use for SSH
// TODO(fxbug.dev/45424): Also return public key
func createSSHKey() (*rsa.PrivateKey, error) {
	privKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return nil, fmt.Errorf("error generating keypair: %s", err)
	}

	return privKey, nil
}

// Writes private key to given path in format usable by SSH
func writeSSHPrivateKeyFile(key *rsa.PrivateKey, path string) error {
	pemData := pem.EncodeToMemory(&pem.Block{
		Type:  "RSA PRIVATE KEY",
		Bytes: x509.MarshalPKCS1PrivateKey(key),
	})

	if err := ioutil.WriteFile(path, pemData, 0600); err != nil {
		return fmt.Errorf("error writing private key file: %s", err)
	}

	return nil
}
