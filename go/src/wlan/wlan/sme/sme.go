package sme

import (
	bindings "fidl/bindings"
)

type Transport interface {
	SendMessage(msg bindings.Payload, ordinal uint32) error
}
