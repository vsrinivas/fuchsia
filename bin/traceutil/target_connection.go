package main

import (
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path"
	"strings"

	"github.com/pkg/sftp"
	"golang.org/x/crypto/ssh"
)

type TargetConnection struct {
	client *ssh.Client
}

func newSigner() (ssh.Signer, error) {
	keyFile := path.Join(buildRoot, "ssh-keys", "id_ed25519")
	key, err := ioutil.ReadFile(keyFile)
	if err != nil {
		return nil, err
	}
	return ssh.ParsePrivateKey(key)
}

func findDefaultTarget() (string, error) {
	netaddr := path.Join(fuchsiaRoot, "out", "build-zircon", "tools", "netaddr")
	output, err := getCommandOutput(netaddr, "--fuchsia")
	output = strings.TrimSpace(output)
	if err != nil {
		return "", errors.New(output)
	}
	return "[" + output + "]", nil
}

func NewTargetConnection(hostname string) (*TargetConnection, error) {
	if hostname == "" {
		defaultHostname, err := findDefaultTarget()
		if err != nil {
			return nil, fmt.Errorf("Can not look up default target: %s",
				err.Error())
		}
		hostname = defaultHostname
	}

	signer, err := newSigner()
	if err != nil {
		return nil, err
	}

	config := &ssh.ClientConfig{
		User: "fuchsia",
		Auth: []ssh.AuthMethod{
			ssh.PublicKeys(signer),
		},
		HostKeyCallback: ssh.InsecureIgnoreHostKey(),
	}

	client, err := ssh.Dial("tcp", hostname+":22", config)
	if err != nil {
		return nil, err
	}
	return &TargetConnection{
		client: client,
	}, nil
}

func (c *TargetConnection) Close() {
	c.client.Close()
}

func (c *TargetConnection) RunCommand(command string) error {
	session, err := c.client.NewSession()
	if err != nil {
		return err
	}
	defer session.Close()

	session.Stdin = os.Stdin
	session.Stdout = os.Stdout
	session.Stderr = os.Stderr
	return session.Run(command)
}

func (c *TargetConnection) GetFile(remotePath string, localPath string) error {
	client, err := sftp.NewClient(c.client)
	if err != nil {
		return err
	}
	defer client.Close()

	remoteFile, err := client.Open(remotePath)
	if err != nil {
		return err
	}
	defer remoteFile.Close()

	localFile, err := os.Create(localPath)
	if err != nil {
		return err
	}
	defer localFile.Close()

	_, err = io.Copy(localFile, remoteFile)
	return err
}
