// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sl4f

import (
	"context"
	"encoding/base64"
	"fmt"
	"os"
)

// FileRead reads path from the target.
func (c *Client) FileRead(path string) ([]byte, error) {
	request := struct {
		Path string `json:"path"`
	}{
		Path: path,
	}
	var response string

	if err := c.call(context.Background(), "file_facade.ReadFile", request, &response); err != nil {
		return nil, err
	}

	return base64.StdEncoding.DecodeString(response)
}

// FileWrite writes buf to path on the target.
func (c *Client) FileWrite(path string, buf []byte) error {
	request := struct {
		Path string `json:"dst"`
		Data string `json:"data"`
	}{
		Path: path,
		Data: base64.StdEncoding.EncodeToString(buf),
	}
	var response string

	if err := c.call(context.Background(), "file_facade.WriteFile", request, &response); err != nil {
		return err
	}

	if response != "Success" {
		return fmt.Errorf("error writing file: %s", response)
	}

	return nil
}

// FileDelete deletes the given path from the target, failing if it does not exist.
func (c *Client) FileDelete(path string) error {
	request := struct {
		Path string `json:"path"`
	}{
		Path: path,
	}
	var response string

	if err := c.call(context.Background(), "file_facade.DeleteFile", request, &response); err != nil {
		return err
	}

	if response == "NotFound" {
		return os.ErrNotExist
	} else if response != "Success" {
		return fmt.Errorf("error writing file: %s", response)
	}
	return nil
}
