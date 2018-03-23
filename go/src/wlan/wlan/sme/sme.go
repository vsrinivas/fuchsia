package sme

import (
	bindings "fidl/bindings2"
)

type Transport interface {
	SendMessage(msg bindings.Payload, ordinal uint32) error
}
