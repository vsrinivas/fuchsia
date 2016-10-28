// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package application

import (
	"fmt"
	"log"

	"fidl/bindings"

	"mojo/public/interfaces/bindings/mojom_types"
	"mojo/public/interfaces/bindings/service_describer"
)

// ServiceDescriberFactory implements ServiceFactory for ServiceDescriber.
// For cleanup purposes, it is also the implementation of a ServiceDescriber.
type ServiceDescriberFactory struct {
	// mapping holds the map from interface names to ServiceDescription's.
	mapping map[string]service_describer.ServiceDescription
	// stubs stores the stubs for connections opened to this factory.
	stubs []*bindings.Stub
	// descriptionFactories maps interface names to ServiceDescriptionFactory's.
	descriptionFactories map[string]*ServiceDescriptionFactory
}

func newServiceDescriberFactory(mapping map[string]service_describer.ServiceDescription) *ServiceDescriberFactory {
	return &ServiceDescriberFactory{
		mapping:              mapping,
		descriptionFactories: make(map[string]*ServiceDescriptionFactory),
	}
}

func (sd *ServiceDescriberFactory) Create(request service_describer.ServiceDescriber_Request) {
	stub := service_describer.NewServiceDescriberStub(request, sd, bindings.GetAsyncWaiter())
	sd.stubs = append(sd.stubs, stub)
	go func() {
		for {
			if err := stub.ServeRequest(); err != nil {
				connectionError, ok := err.(*bindings.ConnectionError)
				if !ok || !connectionError.Closed() {
					log.Println(err)
				}
				break
			}
		}
	}()
}

func (sd *ServiceDescriberFactory) Close() {
	for _, stub := range sd.stubs {
		stub.Close()
	}
	for _, factory := range sd.descriptionFactories {
		for _, stub := range factory.stubs {
			stub.Close()
		}
	}
}

// Helper method for DescribeService
func (sd *ServiceDescriberFactory) getServiceDescriptionFactory(inInterfaceName string) *ServiceDescriptionFactory {
	// Assumes the interface name is in the mapping.
	if desc, ok := sd.descriptionFactories[inInterfaceName]; ok {
		return desc
	}
	sd.descriptionFactories[inInterfaceName] = &ServiceDescriptionFactory{
		impl: sd.mapping[inInterfaceName],
	}
	return sd.descriptionFactories[inInterfaceName]
}

func (sd *ServiceDescriberFactory) DescribeService(inInterfaceName string, inDescriptionRequest service_describer.ServiceDescription_Request) (err error) {
	if _, ok := sd.mapping[inInterfaceName]; ok {
		sd.getServiceDescriptionFactory(inInterfaceName).Create(inDescriptionRequest)
		return nil
	}
	return fmt.Errorf("The interface %s is unknown by this application", inInterfaceName)
}

// ServiceDescriptionFactory implements ServiceFactory for ServiceDescription.
type ServiceDescriptionFactory struct {
	// stubs stores the stubs for connections opened to this factory.
	stubs []*bindings.Stub
	// impl is the ServiceDescription implementation served by this factory.
	impl service_describer.ServiceDescription
}

func (serviceDescriptionFactory *ServiceDescriptionFactory) Create(request service_describer.ServiceDescription_Request) {
	stub := service_describer.NewServiceDescriptionStub(request, serviceDescriptionFactory.impl, bindings.GetAsyncWaiter())
	serviceDescriptionFactory.stubs = append(serviceDescriptionFactory.stubs, stub)
	go func() {
		for {
			if err := stub.ServeRequest(); err != nil {
				connectionError, ok := err.(*bindings.ConnectionError)
				if !ok || !connectionError.Closed() {
					log.Println(err)
				}
				break
			}
		}
	}()
}

// ObjectWithMojomTypeSupport is an interface implemented by pointers to
// Mojo structs, enums, interface requests and union variants, but only if the
// support of runtime mojom type information was enabled at build time.
type ObjectWithMojomTypeSupport interface {
	// MojomType returns the UserDefinedType that describes the Mojom
	// type of this object. To obtain the UserDefinedType for Mojom types recursively
	// contained in the returned UserDefinedType, look in the map returned
	// by the function AllMojomTypes().
	MojomType() mojom_types.UserDefinedType

	// AllMojomTypes returns a map that contains the UserDefinedType for
	// all Mojom types in the complete type graph of the Mojom type of this object.
	AllMojomTypes() map[string]mojom_types.UserDefinedType
}
