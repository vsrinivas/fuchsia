// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Library = `
{{- define "GenerateLibraryFile" -}}
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:typed_data';

import 'package:fidl/fidl.dart' as $fidl;
import 'package:meta/meta.dart';
import 'package:zircon/zircon.dart';

// ignore_for_file: always_specify_types
// ignore_for_file: avoid_positional_boolean_parameters
// ignore_for_file: avoid_returning_null
// ignore_for_file: camel_case_types
// ignore_for_file: cascade_invocations
// ignore_for_file: constant_identifier_names
// ignore_for_file: non_constant_identifier_names
// ignore_for_file: one_member_abstracts
// ignore_for_file: prefer_constructors_over_static_methods
// ignore_for_file: public_member_api_docs
// ignore_for_file: unused_import
// ignore_for_file: unused_local_variable

{{ range $enum := .Enums -}}
{{ template "EnumDeclaration" $enum }}
{{ end -}}
{{ range $union := .Unions -}}
{{ template "UnionDeclaration" $union }}
{{ end -}}
{{ range $struct := .Structs -}}
{{ template "StructDeclaration" $struct }}
{{ end -}}
{{ range $interface := .Interfaces -}}
{{ template "InterfaceDeclaration" $interface }}
{{ end -}}
{{- end -}}
`
