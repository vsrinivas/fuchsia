// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sl4f

import (
	"context"
	"encoding/base64"
	"encoding/json"
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

type PathMetadata struct {
	Mode os.FileMode
	Size int64
}

// PathStat deletes the given path from the target, failing if it does not exist.
func (c *Client) PathStat(path string) (PathMetadata, error) {
	request := struct {
		Path string `json:"path"`
	}{
		Path: path,
	}
	var raw_response json.RawMessage

	if err := c.call(context.Background(), "file_facade.Stat", request, &raw_response); err != nil {
		return PathMetadata{}, err
	}

	// raw_response is either a json encoded string literal or an object
	// with a single key called "Success".
	if string(raw_response) == `"NotFound"` {
		return PathMetadata{}, os.ErrNotExist
	}
	var response struct {
		Success struct {
			Kind string
			Size int64
		}
	}
	if err := json.Unmarshal(raw_response, &response); err != nil {
		return PathMetadata{}, fmt.Errorf("error statting path: %s", raw_response)
	}

	metadata := PathMetadata{
		Size: response.Success.Size,
	}

	switch response.Success.Kind {
	case "file":
		metadata.Mode = 0
	case "directory":
		metadata.Mode = os.ModeDir
	default:
		metadata.Mode = os.ModeIrregular
	}

	return metadata, nil
}
