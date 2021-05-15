// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"reflect"
)

// A Handle provides a way to persist and restore information about an Instance
// between separate invocations of the tool.
//
// In this case, it is a simple wrapper around a JSON file on disk, kept as
// simple as possible by updating only in bulk and avoiding any caching.
//
// It is not safe to use the same Handle simultaneously in multiple processes.
// This does not pose an issue under expected use, however, because the only
// time Handles are currently modified after creation is when they are deleted
// by a call to `stop_instance`. The only potential consequence would be an
// attempt to connect to an already-stopped instance, which will throw an
// appropriate error.

type Handle string

// HandleData represents the top-level structure of the data stored within the
// Handle.
type HandleData struct {
	connector Connector
	launcher  Launcher
}

// annotatedHandleData is the format of the serialized data. Type information
// is stored alongside the objects so that they can be properly deserialized.
type annotatedHandleData struct {
	ConnectorType string
	Connector     json.RawMessage

	LauncherType string
	Launcher     json.RawMessage
}

// NewHandle allocates a new handle for use. It will persist until explicitly Released.
func NewHandle() (Handle, error) {
	tmpFile, err := ioutil.TempFile("", "undercoat_*.handle")
	if err != nil {
		return "", fmt.Errorf("error allocating tempfile: %s", err)
	}
	defer tmpFile.Close()

	// Start with an empty JSON object
	tmpFile.WriteString("{}")

	return Handle(tmpFile.Name()), nil
}

// NewHandleWithData is a convenience function for populating a new Handle
// at the same time as creating it.
func NewHandleWithData(initialData HandleData) (Handle, error) {
	handle, err := NewHandle()
	if err != nil {
		return "", fmt.Errorf("error creating handle: %s", err)
	}

	if err := handle.SetData(initialData); err != nil {
		handle.Release()
		return "", fmt.Errorf("error initializing handle: %s", err)
	}

	return handle, nil
}

// LoadHandleFromString recreates a previously-created Handle from its string
// representation as returned by Serialize()
func LoadHandleFromString(s string) (Handle, error) {
	h := Handle(s)
	// Make sure we can load it, and error out early if not
	if _, err := h.loadFromDisk(); err != nil {
		return "", fmt.Errorf("error loading handle: %s", err)
	}
	return h, nil
}

// Serialize returns a printable representation of the Handle
func (h Handle) Serialize() string {
	return string(h)
}

// GetData will load and return data from the Handle, as previously stored by SetData.
func (h Handle) GetData() (*HandleData, error) {
	rawData, err := h.loadFromDisk()
	if err != nil {
		return nil, fmt.Errorf("error loading handle: %s", err)
	}

	var data HandleData

	switch rawData.ConnectorType {
	case "SSHConnector":
		data.connector = new(SSHConnector)
	case "":
		// connector is empty
	default:
		return nil, fmt.Errorf("unknown connector type: %q", rawData.ConnectorType)
	}
	if rawData.ConnectorType != "" {
		if err := json.Unmarshal(rawData.Connector, &data.connector); err != nil {
			return nil, fmt.Errorf("error unmarshaling connector: %s", err)
		}
	}

	switch rawData.LauncherType {
	case "QemuLauncher":
		data.launcher = NewQemuLauncher(nil)
	case "":
		// launcher is empty
	default:
		return nil, fmt.Errorf("unknown launcher type: %q", rawData.LauncherType)
	}
	if rawData.LauncherType != "" {
		if err := json.Unmarshal(rawData.Launcher, &data.launcher); err != nil {
			return nil, fmt.Errorf("error unmarshaling launcher: %s", err)
		}
	}

	return &data, nil
}

// SetData stores all exported fields of the given data into the Handle, so
// they can later be retrieved with GetData. Any existing data in the Handle
// will be overwritten.
func (h Handle) SetData(data HandleData) error {
	// Make sure we haven't been released
	if _, err := h.loadFromDisk(); err != nil {
		return fmt.Errorf("error loading handle: %s", err)
	}

	var rawData annotatedHandleData

	// Save type information
	if data.connector != nil {
		rawData.ConnectorType = reflect.TypeOf(data.connector).Elem().Name()
	}
	if data.launcher != nil {
		rawData.LauncherType = reflect.TypeOf(data.launcher).Elem().Name()
	}

	// Serialize sub-objects
	connectorData, err := json.Marshal(data.connector)
	if err != nil {
		return fmt.Errorf("error serializing connector: %s", err)
	}
	rawData.Connector = connectorData

	launcherData, err := json.Marshal(data.launcher)
	if err != nil {
		return fmt.Errorf("error serializing launcher: %s", err)
	}
	rawData.Launcher = launcherData

	// Serialize outer object
	jsonData, err := json.Marshal(rawData)
	if err != nil {
		return fmt.Errorf("error serializing handle: %s", err)
	}

	// Persist immediately
	if err := ioutil.WriteFile(string(h), jsonData, 0600); err != nil {
		return fmt.Errorf("error writing handle: %s", err)
	}

	return nil
}

// Release removes any resources used by the Handle. After calling Release, the handle
// will become invalid and cannot be further used.
func (h Handle) Release() {
	os.Remove(string(h))
}

func (h Handle) loadFromDisk() (*annotatedHandleData, error) {
	jsonData, err := ioutil.ReadFile(string(h))
	if err != nil {
		return nil, fmt.Errorf("error opening file: %s", err)
	}

	var data annotatedHandleData

	if err := json.Unmarshal(jsonData, &data); err != nil {
		return nil, fmt.Errorf("error unmarshaling: %s", err)
	}

	return &data, nil
}
