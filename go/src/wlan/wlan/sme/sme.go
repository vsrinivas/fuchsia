package sme

import (
	bindings "syscall/zx/fidl"
)

type Transport interface {
	SendMessage(msg bindings.Payload, ordinal uint32) error
}
