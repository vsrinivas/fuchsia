// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sl4f

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"strings"
	"sync/atomic"
	"time"

	"fuchsia.googlesource.com/host_target_testing/sshclient"
	"golang.org/x/crypto/ssh"
)

// Client is a wrapper around sl4f that supports auto-installing and starting
// sl4f on the target.
//
// If the target already has sl4f running, this client will not start a new
// instance.
//
// This client requires the target to contain the "pkgctl" binary and for the
// target to have the "run" and "sl4f" packages available at
// "fuchsia-pkg://host_target_testing".
type Client struct {
	sshClient *sshclient.Client
	url       string
	repoName  string
	seq       uint64
	server    *ssh.Session
}

func NewClient(sshClient *sshclient.Client, addr string, repoName string) (*Client, error) {
	c := &Client{
		sshClient: sshClient,
		repoName:  repoName,
		url:       fmt.Sprintf("http://%s", strings.ReplaceAll(addr, "%", "%25")),
	}

	if err := c.connect(); err != nil {
		return nil, err
	}

	return c, nil
}

func (c *Client) Close() {
	c.disconnect()
}

func (c *Client) connect() error {
	// If an ssh connection re-establishes without a reboot, sl4f may already be running.
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	if err := c.ping(ctx); err == nil {
		cancel()
		return nil
	}
	cancel()

	// In order to run components via SSH, we need the `run` package to be
	// cached on the device. Since builds can be configured to not
	// automatically cache packages, we need to explicitly resolve it.
	cmd := fmt.Sprintf("pkgctl resolve fuchsia-pkg://%s/run/0", c.repoName)
	if err := c.sshClient.Run(cmd, os.Stdout, os.Stderr); err != nil {
		log.Printf("unable to resolve `run` package: %s", err)
		return err
	}

	// Additionally, we must resolve the sl4f package before attempting to
	// run it if the build is not configured to automatically cache
	// packages.
	cmd = fmt.Sprintf("pkgctl resolve fuchsia-pkg://%s/sl4f/0", c.repoName)
	if err := c.sshClient.Run(cmd, os.Stdout, os.Stderr); err != nil {
		log.Printf("unable to resolve `sl4f` package: %s", err)
		return err
	}

	// Start the sl4f daemon.
	cmd = fmt.Sprintf("run fuchsia-pkg://%s/sl4f#meta/sl4f.cmx", c.repoName)
	server, err := c.sshClient.Start(cmd, os.Stdout, os.Stderr)
	if err != nil {
		log.Printf("unable to launch sl4f: %s", err)
		return err
	}

	// Wait a few seconds for it to respond to requests.
	ctx, cancel = context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	for ctx.Err() == nil {
		if err := c.ping(ctx); err == nil {
			c.server = server
			return nil
		}
		time.Sleep(time.Second)
	}

	log.Printf("unable to ping sl4f: %s", ctx.Err())
	server.Close()
	return ctx.Err()
}

func (c *Client) disconnect() {
	if c.server != nil {
		// Closing the session kills the remote command.
		c.server.Close()
		c.server = nil
	}
}

// ping attempts to perform an sl4f command that should always succeed if the server is up.
func (c *Client) ping(ctx context.Context) error {
	request := struct {
		Path string `json:"path"`
	}{
		Path: "/system/meta",
	}
	var response string

	if err := c.call(context.Background(), "file_facade.ReadFile", request, &response); err != nil {
		return err
	}

	return nil
}

func (c *Client) call(ctx context.Context, method string, params interface{}, result interface{}) error {
	type request struct {
		Method string      `json:"method"`
		Id     string      `json:"id"`
		Params interface{} `json:"params"`
	}

	id := fmt.Sprintf("%d", atomic.AddUint64(&c.seq, 1))
	body, err := json.Marshal(request{
		Method: method,
		Id:     id,
		Params: params,
	})
	if err != nil {
		return err
	}

	req, err := http.NewRequest("GET", c.url, bytes.NewReader(body))
	if err != nil {
		return err
	}
	req.Header.Add("Content-Type", "application/json")

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	var response struct {
		Id     string          `json:"id"`
		Result json.RawMessage `json:"result"`
		Error  json.RawMessage `json:"error"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&response); err != nil {
		return err
	}

	// TODO(39973) sl4f currently response with different id values, but
	// over HTTP, we can safely assume this response is for the request we
	// made over this connection
	//if response.Id != id {
	//	return fmt.Errorf("server responded with invalid ID: %q != %q", id, response.Id)
	//}

	var response_error string
	if err := json.Unmarshal(response.Error, &response_error); err != nil {
		return fmt.Errorf("unable to decode error from server: %s", response.Error)
	}

	if response_error != "" {
		return fmt.Errorf("error from server: %s", response_error)
	}

	if err := json.Unmarshal(response.Result, result); err != nil {
		return err
	}

	return nil
}
