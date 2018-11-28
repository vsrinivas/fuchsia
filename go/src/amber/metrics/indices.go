package metrics

import (
	"fmt"
	"strings"
)

type Initiator uint32

const (
	InitiatorAutomatic Initiator = iota
	InitiatorManual
)

func (i *Initiator) Parse(s string) error {
	switch strings.ToLower(s) {
	case "automatic":
		*i = InitiatorAutomatic
	case "manual":
		*i = InitiatorManual
	default:
		return fmt.Errorf("invalid initiator: %q", s)
	}
	return nil
}

func (i Initiator) String() string {
	switch i {
	case InitiatorAutomatic:
		return "automatic"
	case InitiatorManual:
		return "manual"
	}
	return "unknown"
}

type Phase uint32

const (
	PhaseEndToEnd Phase = iota
	PhaseTufUpdate
	PhasePackageDownload
	PhaseImageWrite
	PhaseSuccessPendingReboot
	PhaseSuccess
)
