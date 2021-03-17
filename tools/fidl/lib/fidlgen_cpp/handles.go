// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"

	fidl "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type HandleInformation struct {
	ObjectType string
	Rights     string
}

func (c *compiler) fieldHandleInformation(val *fidl.Type) *HandleInformation {
	if val.ElementType != nil {
		return c.fieldHandleInformation(val.ElementType)
	}
	if val.Kind == fidl.RequestType {
		// TODO(fxbug.dev/72222): Implement handle rights on server protocol endpoints.
		return nil
	}
	if val.Kind == fidl.IdentifierType {
		declInfo, ok := c.decls[val.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Identifier))
		}
		if declInfo.Type == fidl.ProtocolDeclType {
			// TODO(fxbug.dev/72222): Implement handle rights on client protocol endpoints.
			return nil
		}
		// Handle rights are only attached to handle fields or vector/arrays thereof.
		return nil
	}
	if val.Kind == fidl.HandleType {
		subtype, ok := handleSubtypeConsts[val.HandleSubtype]
		if !ok {
			panic(fmt.Sprintf("unknown handle type for const: %v", val))
		}
		return &HandleInformation{
			ObjectType: fmt.Sprintf("ZX_OBJ_TYPE_%s", subtype),
			Rights:     fmt.Sprintf("0x%x", val.HandleRights),
		}
	}
	return nil
}

var handleSubtypeConsts = map[fidl.HandleSubtype]string{
	fidl.Bti:          "BTI",
	fidl.Channel:      "CHANNEL",
	fidl.Clock:        "CLOCK",
	fidl.DebugLog:     "LOG",
	fidl.Event:        "EVENT",
	fidl.Eventpair:    "EVENTPAIR",
	fidl.Exception:    "EXCEPTION",
	fidl.Fifo:         "FIFO",
	fidl.Guest:        "GUEST",
	fidl.Handle:       "NONE",
	fidl.Interrupt:    "INTERRUPT",
	fidl.Iommu:        "IOMMU",
	fidl.Job:          "JOB",
	fidl.Pager:        "PAGER",
	fidl.PciDevice:    "PCI_DEVICE",
	fidl.Pmt:          "PMT",
	fidl.Port:         "PORT",
	fidl.Process:      "PROCESS",
	fidl.Profile:      "PROFILE",
	fidl.Resource:     "RESOURCE",
	fidl.Socket:       "SOCKET",
	fidl.Stream:       "STREAM",
	fidl.SuspendToken: "SUSPEND_TOKEN",
	fidl.Thread:       "THREAD",
	fidl.Time:         "TIMER",
	fidl.Vcpu:         "VCPU",
	fidl.Vmar:         "VMAR",
	fidl.Vmo:          "VMO",
}
