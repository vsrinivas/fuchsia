// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package service_provider_impl

import (
	"syscall/zx"
)

type ServiceFactory interface {
	// Name returns the name of provided fidl service.
	Name() string

	// Create binds an implementation of fidl service to the provided
	// channel and runs it.
	Create(h zx.Handle)
}

type ServiceProviderImpl struct {
	factories map[string]ServiceFactory
}

func New() *ServiceProviderImpl {
	return &ServiceProviderImpl{make(map[string]ServiceFactory)}
}

func (spi *ServiceProviderImpl) ConnectToService(name string, handle zx.Handle) error {
	factory, ok := spi.factories[name]
	if !ok {
		handle.Close()
		return nil
	}
	factory.Create(handle)
	return nil
}

func (spi *ServiceProviderImpl) AddService(factory ServiceFactory) {
	spi.factories[factory.Name()] = factory
}
