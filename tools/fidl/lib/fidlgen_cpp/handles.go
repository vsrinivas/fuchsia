// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

type HandleInformation struct {
	ObjectType string
	Rights     string
}

func (c *compiler) fieldHandleInformation(val *fidlgen.Type) *HandleInformation {
	if val.ElementType != nil {
		return c.fieldHandleInformation(val.ElementType)
	}
	if val.Kind == fidlgen.RequestType {
		return &HandleInformation{
			ObjectType: "ZX_OBJ_TYPE_CHANNEL",
			Rights:     "ZX_DEFAULT_CHANNEL_RIGHTS",
		}
	}
	if val.Kind == fidlgen.IdentifierType {
		declInfo, ok := c.decls[val.Identifier]
		if !ok {
			panic(fmt.Sprintf("unknown identifier: %v", val.Identifier))
		}
		if declInfo.Type == fidlgen.ProtocolDeclType {
			return &HandleInformation{
				ObjectType: "ZX_OBJ_TYPE_CHANNEL",
				Rights:     "ZX_DEFAULT_CHANNEL_RIGHTS",
			}
		}
		// Handle rights are only attached to handle fields or vector/arrays thereof.
		return nil
	}
	if val.Kind == fidlgen.HandleType {
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

var handleSubtypeConsts = map[fidlgen.HandleSubtype]string{
	fidlgen.Bti:          "BTI",
	fidlgen.Channel:      "CHANNEL",
	fidlgen.Clock:        "CLOCK",
	fidlgen.DebugLog:     "LOG",
	fidlgen.Event:        "EVENT",
	fidlgen.Eventpair:    "EVENTPAIR",
	fidlgen.Exception:    "EXCEPTION",
	fidlgen.Fifo:         "FIFO",
	fidlgen.Guest:        "GUEST",
	fidlgen.Handle:       "NONE",
	fidlgen.Interrupt:    "INTERRUPT",
	fidlgen.Iommu:        "IOMMU",
	fidlgen.Job:          "JOB",
	fidlgen.Pager:        "PAGER",
	fidlgen.PciDevice:    "PCI_DEVICE",
	fidlgen.Pmt:          "PMT",
	fidlgen.Port:         "PORT",
	fidlgen.Process:      "PROCESS",
	fidlgen.Profile:      "PROFILE",
	fidlgen.Resource:     "RESOURCE",
	fidlgen.Socket:       "SOCKET",
	fidlgen.Stream:       "STREAM",
	fidlgen.SuspendToken: "SUSPEND_TOKEN",
	fidlgen.Thread:       "THREAD",
	fidlgen.Time:         "TIMER",
	fidlgen.Vcpu:         "VCPU",
	fidlgen.Vmar:         "VMAR",
	fidlgen.Vmo:          "VMO",
}
