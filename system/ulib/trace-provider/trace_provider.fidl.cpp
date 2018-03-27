// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// NOTE: This file was generated using the fidl tool then manually modified
// to workaround limitations of the current code generator including table
// symbol collisions and invalid interface types in structs.
//

#include <lib/fidl/internal.h>

extern "C" {

static const fidl_type_t HandlevmononnullableTable = fidl_type_t(::fidl::FidlCodedHandle(ZX_OBJ_TYPE_VMO, ::fidl::kNonnullable));

static const fidl_type_t HandleeventpairnonnullableTable = fidl_type_t(::fidl::FidlCodedHandle(ZX_OBJ_TYPE_EVENT_PAIR, ::fidl::kNonnullable));

static const fidl_type_t String100nonnullableTable = fidl_type_t(::fidl::FidlCodedString(100, ::fidl::kNonnullable));

static const fidl_type_t VectorString100nonnullable100nonnullableTable = fidl_type_t(::fidl::FidlCodedVector(&String100nonnullableTable, 100, 16, ::fidl::kNonnullable));

extern const fidl_type_t _ProviderStartRequestTable;
static const ::fidl::FidlField ProviderStartRequestFields[] = {
    ::fidl::FidlField(&HandlevmononnullableTable, 16),
    ::fidl::FidlField(&HandleeventpairnonnullableTable, 20),
    ::fidl::FidlField(&VectorString100nonnullable100nonnullableTable, 24)};
const fidl_type_t _ProviderStartRequestTable = fidl_type_t(::fidl::FidlCodedStruct(ProviderStartRequestFields, 3, 40, "trace_link/Provider.Start#Request"));

extern const fidl_type_t _ProviderStopRequestTable;
static const ::fidl::FidlField ProviderStopRequestFields[] = {};
const fidl_type_t _ProviderStopRequestTable = fidl_type_t(::fidl::FidlCodedStruct(ProviderStopRequestFields, 0, 16, "trace_link/Provider.Stop#Request"));

static const fidl_type_t InterfaceProvidernonnullableTable = fidl_type_t(::fidl::FidlCodedHandle(ZX_OBJ_TYPE_CHANNEL, ::fidl::kNonnullable));

extern const fidl_type_t _RegistryRegisterTraceProviderRequestTable;
static const ::fidl::FidlField RegistryRegisterTraceProviderRequestFields[] = {
    ::fidl::FidlField(&InterfaceProvidernonnullableTable, 16)};
const fidl_type_t _RegistryRegisterTraceProviderRequestTable = fidl_type_t(::fidl::FidlCodedStruct(RegistryRegisterTraceProviderRequestFields, 1, 20,
                                                                                                   "trace_link/Registry.RegisterTraceProvider#Request"));

} // extern "C"
