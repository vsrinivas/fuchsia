// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package resultstore

import (
	"context"
	"encoding/json"
	"fmt"
)

const (
	// TestUUIDKey references a UUID to use for testing.
	TestUUIDKey = "test-uuid"

	// AuthTokenKey references the auth token to use in ResultStore requests.
	AuthTokenKey = "auth-token"
)

// TestUUID gets the test UUID, if present. Errors if the value is missing or cannot be
// read from the Context.
func TestUUID(ctx context.Context) (string, error) {
	var uuid string
	err := get(ctx, TestUUIDKey, &uuid)
	return uuid, err
}

// SetTestUUID sets a UUID to use for testing.
func SetTestUUID(ctx context.Context, uuid string) (context.Context, error) {
	return set(ctx, TestUUIDKey, uuid)
}

// AuthToken gets the auth token to use in ResultStore requests.
func AuthToken(ctx context.Context) (string, error) {
	var token string
	err := get(ctx, AuthTokenKey, &token)
	return token, err
}

// SetAuthToken sets the auth token to use in ResultStore requests.
func SetAuthToken(ctx context.Context, value string) (context.Context, error) {
	return set(ctx, AuthTokenKey, value)
}

// Set associates a JSON encodable value with a key in a Context object.
func set(ctx context.Context, key string, value interface{}) (context.Context, error) {
	data, err := json.Marshal(value)
	if err != nil {
		return nil, err
	}
	return context.WithValue(ctx, key, data), nil
}

// Get reads a value from a Context into `out`.  Returns true iff the given key is present
// in the Context, even if the key cannot be read into `out`.
func get(ctx context.Context, key string, out interface{}) error {
	value := ctx.Value(key)
	if value == nil {
		return fmt.Errorf("not found %v", key)
	}
	bytes, ok := value.([]byte)
	if !ok {
		return fmt.Errorf("expected a []byte value for %v but got %v", key, value)
	}
	return json.Unmarshal(bytes, out)
}
