// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Helpers = `
{{- define "CreateTxnHeader" }}
  params.message()->_hdr = {};
  params.message()->_hdr.flags[0] = 0;
  params.message()->_hdr.flags[1] = 0;
  params.message()->_hdr.flags[2] = 0;
  params.message()->_hdr.magic_number = kFidlWireFormatMagicNumberInitial;
  params.message()->_hdr.ordinal = {{ .Ordinals.Write.Name }};
{{- end }}

{{- define "SetTxnHeader" }}
  _response._hdr.flags[0] = 0;
  _response._hdr.flags[1] = 0;
  _response._hdr.flags[2] = 0;
  _response._hdr.magic_number = kFidlWireFormatMagicNumberInitial;
  _response._hdr.ordinal = {{ .Ordinals.Write.Name }};
{{- end }}
`
