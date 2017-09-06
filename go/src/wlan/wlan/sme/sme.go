package sme

import (
	bindings "fidl/bindings"
)

type Transport interface {
	SendMessage(msg bindings.Payload, ordinal int32) error
}