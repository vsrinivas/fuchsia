// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///
//  Generated code. Do not modify.
//  source: report.proto
//
// @dart = 2.12
// ignore_for_file: sort_unnamed_constructors_first,avoid_as,prefer_constructors_over_static_methods,annotate_overrides,camel_case_types,unnecessary_const,non_constant_identifier_names,library_prefixes,unused_import,unused_shown_name,return_of_invalid_type,unnecessary_this,prefer_final_fields

import 'dart:core' as $core;

import 'package:protobuf/protobuf.dart' as $pb;

class SizeInfo extends $pb.GeneratedMessage {
  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      const $core.bool.fromEnvironment('protobuf.omit_message_names')
          ? ''
          : 'SizeInfo',
      package: const $pb.PackageName(
          const $core.bool.fromEnvironment('protobuf.omit_message_names')
              ? ''
              : 'bloaty_report'),
      createEmptyInstance: create)
    ..a<$core.int>(
        1,
        const $core.bool.fromEnvironment('protobuf.omit_field_names')
            ? ''
            : 'fileActual',
        $pb.PbFieldType.OU3)
    ..a<$core.int>(
        2,
        const $core.bool.fromEnvironment('protobuf.omit_field_names')
            ? ''
            : 'vmActual',
        $pb.PbFieldType.OU3)
    ..hasRequiredFields = false;

  SizeInfo._() : super();
  factory SizeInfo() => create();
  factory SizeInfo.fromBuffer($core.List<$core.int> i,
          [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(i, r);
  factory SizeInfo.fromJson($core.String i,
          [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(i, r);
  @$core.Deprecated('Using this can add significant overhead to your binary. '
      'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
      'Will be removed in next major version')
  SizeInfo clone() => SizeInfo()..mergeFromMessage(this);
  @$core.Deprecated('Using this can add significant overhead to your binary. '
      'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
      'Will be removed in next major version')
  SizeInfo copyWith(void Function(SizeInfo) updates) =>
      super.copyWith((message) => updates(message as SizeInfo))
          as SizeInfo; // ignore: deprecated_member_use
  $pb.BuilderInfo get info_ => _i;
  @$core.pragma('dart2js:noInline')
  static SizeInfo create() => SizeInfo._();
  SizeInfo createEmptyInstance() => create();
  static $pb.PbList<SizeInfo> createRepeated() => $pb.PbList<SizeInfo>();
  @$core.pragma('dart2js:noInline')
  static SizeInfo getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<SizeInfo>(create);
  static SizeInfo? _defaultInstance;

  @$pb.TagNumber(1)
  $core.int get fileActual => $_getIZ(0);
  @$pb.TagNumber(1)
  set fileActual($core.int v) {
    $_setUnsignedInt32(0, v);
  }

  @$pb.TagNumber(1)
  $core.bool hasFileActual() => $_has(0);
  @$pb.TagNumber(1)
  void clearFileActual() => clearField(1);

  @$pb.TagNumber(2)
  $core.int get vmActual => $_getIZ(1);
  @$pb.TagNumber(2)
  set vmActual($core.int v) {
    $_setUnsignedInt32(1, v);
  }

  @$pb.TagNumber(2)
  $core.bool hasVmActual() => $_has(1);
  @$pb.TagNumber(2)
  void clearVmActual() => clearField(2);
}

class Symbol extends $pb.GeneratedMessage {
  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      const $core.bool.fromEnvironment('protobuf.omit_message_names')
          ? ''
          : 'Symbol',
      package: const $pb.PackageName(
          const $core.bool.fromEnvironment('protobuf.omit_message_names')
              ? ''
              : 'bloaty_report'),
      createEmptyInstance: create)
    ..aOM<SizeInfo>(
        1,
        const $core.bool.fromEnvironment('protobuf.omit_field_names')
            ? ''
            : 'sizes',
        subBuilder: SizeInfo.create)
    ..aOS(2,
        const $core.bool.fromEnvironment('protobuf.omit_field_names') ? '' : 'name')
    ..aOS(3, const $core.bool.fromEnvironment('protobuf.omit_field_names') ? '' : 'maybeRustCrate')
    ..hasRequiredFields = false;

  Symbol._() : super();
  factory Symbol() => create();
  factory Symbol.fromBuffer($core.List<$core.int> i,
          [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(i, r);
  factory Symbol.fromJson($core.String i,
          [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(i, r);
  @$core.Deprecated('Using this can add significant overhead to your binary. '
      'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
      'Will be removed in next major version')
  Symbol clone() => Symbol()..mergeFromMessage(this);
  @$core.Deprecated('Using this can add significant overhead to your binary. '
      'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
      'Will be removed in next major version')
  Symbol copyWith(void Function(Symbol) updates) =>
      super.copyWith((message) => updates(message as Symbol))
          as Symbol; // ignore: deprecated_member_use
  $pb.BuilderInfo get info_ => _i;
  @$core.pragma('dart2js:noInline')
  static Symbol create() => Symbol._();
  Symbol createEmptyInstance() => create();
  static $pb.PbList<Symbol> createRepeated() => $pb.PbList<Symbol>();
  @$core.pragma('dart2js:noInline')
  static Symbol getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Symbol>(create);
  static Symbol? _defaultInstance;

  @$pb.TagNumber(1)
  SizeInfo get sizes => $_getN(0);
  @$pb.TagNumber(1)
  set sizes(SizeInfo v) {
    setField(1, v);
  }

  @$pb.TagNumber(1)
  $core.bool hasSizes() => $_has(0);
  @$pb.TagNumber(1)
  void clearSizes() => clearField(1);
  @$pb.TagNumber(1)
  SizeInfo ensureSizes() => $_ensure(0);

  @$pb.TagNumber(2)
  $core.String get name => $_getSZ(1);
  @$pb.TagNumber(2)
  set name($core.String v) {
    $_setString(1, v);
  }

  @$pb.TagNumber(2)
  $core.bool hasName() => $_has(1);
  @$pb.TagNumber(2)
  void clearName() => clearField(2);

  @$pb.TagNumber(3)
  $core.String get maybeRustCrate => $_getSZ(2);
  @$pb.TagNumber(3)
  set maybeRustCrate($core.String v) {
    $_setString(2, v);
  }

  @$pb.TagNumber(3)
  $core.bool hasMaybeRustCrate() => $_has(2);
  @$pb.TagNumber(3)
  void clearMaybeRustCrate() => clearField(3);
}

class CompileUnit extends $pb.GeneratedMessage {
  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      const $core.bool.fromEnvironment('protobuf.omit_message_names')
          ? ''
          : 'CompileUnit',
      package: const $pb.PackageName(
          const $core.bool.fromEnvironment('protobuf.omit_message_names')
              ? ''
              : 'bloaty_report'),
      createEmptyInstance: create)
    ..aOM<SizeInfo>(
        1, const $core.bool.fromEnvironment('protobuf.omit_field_names') ? '' : 'sizes',
        subBuilder: SizeInfo.create)
    ..pc<Symbol>(
        2,
        const $core.bool.fromEnvironment('protobuf.omit_field_names') ? '' : 'symbols',
        $pb.PbFieldType.PM,
        subBuilder: Symbol.create)
    ..aOS(3, const $core.bool.fromEnvironment('protobuf.omit_field_names') ? '' : 'name')
    ..hasRequiredFields = false;

  CompileUnit._() : super();
  factory CompileUnit() => create();
  factory CompileUnit.fromBuffer($core.List<$core.int> i,
          [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(i, r);
  factory CompileUnit.fromJson($core.String i,
          [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(i, r);
  @$core.Deprecated('Using this can add significant overhead to your binary. '
      'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
      'Will be removed in next major version')
  CompileUnit clone() => CompileUnit()..mergeFromMessage(this);
  @$core.Deprecated('Using this can add significant overhead to your binary. '
      'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
      'Will be removed in next major version')
  CompileUnit copyWith(void Function(CompileUnit) updates) =>
      super.copyWith((message) => updates(message as CompileUnit))
          as CompileUnit; // ignore: deprecated_member_use
  $pb.BuilderInfo get info_ => _i;
  @$core.pragma('dart2js:noInline')
  static CompileUnit create() => CompileUnit._();
  CompileUnit createEmptyInstance() => create();
  static $pb.PbList<CompileUnit> createRepeated() => $pb.PbList<CompileUnit>();
  @$core.pragma('dart2js:noInline')
  static CompileUnit getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<CompileUnit>(create);
  static CompileUnit? _defaultInstance;

  @$pb.TagNumber(1)
  SizeInfo get sizes => $_getN(0);
  @$pb.TagNumber(1)
  set sizes(SizeInfo v) {
    setField(1, v);
  }

  @$pb.TagNumber(1)
  $core.bool hasSizes() => $_has(0);
  @$pb.TagNumber(1)
  void clearSizes() => clearField(1);
  @$pb.TagNumber(1)
  SizeInfo ensureSizes() => $_ensure(0);

  @$pb.TagNumber(2)
  $core.List<Symbol> get symbols => $_getList(1);

  @$pb.TagNumber(3)
  $core.String get name => $_getSZ(2);
  @$pb.TagNumber(3)
  set name($core.String v) {
    $_setString(2, v);
  }

  @$pb.TagNumber(3)
  $core.bool hasName() => $_has(2);
  @$pb.TagNumber(3)
  void clearName() => clearField(3);
}

class Report extends $pb.GeneratedMessage {
  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      const $core.bool.fromEnvironment('protobuf.omit_message_names')
          ? ''
          : 'Report',
      package: const $pb.PackageName(
          const $core.bool.fromEnvironment('protobuf.omit_message_names')
              ? ''
              : 'bloaty_report'),
      createEmptyInstance: create)
    ..pc<CompileUnit>(
        1,
        const $core.bool.fromEnvironment('protobuf.omit_field_names')
            ? ''
            : 'compileUnits',
        $pb.PbFieldType.PM,
        subBuilder: CompileUnit.create)
    ..a<$core.int>(
        2,
        const $core.bool.fromEnvironment('protobuf.omit_field_names') ? '' : 'fileTotal',
        $pb.PbFieldType.OU3)
    ..a<$core.int>(3, const $core.bool.fromEnvironment('protobuf.omit_field_names') ? '' : 'vmTotal', $pb.PbFieldType.OU3)
    ..hasRequiredFields = false;

  Report._() : super();
  factory Report() => create();
  factory Report.fromBuffer($core.List<$core.int> i,
          [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(i, r);
  factory Report.fromJson($core.String i,
          [$pb.ExtensionRegistry r = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(i, r);
  @$core.Deprecated('Using this can add significant overhead to your binary. '
      'Use [GeneratedMessageGenericExtensions.deepCopy] instead. '
      'Will be removed in next major version')
  Report clone() => Report()..mergeFromMessage(this);
  @$core.Deprecated('Using this can add significant overhead to your binary. '
      'Use [GeneratedMessageGenericExtensions.rebuild] instead. '
      'Will be removed in next major version')
  Report copyWith(void Function(Report) updates) =>
      super.copyWith((message) => updates(message as Report))
          as Report; // ignore: deprecated_member_use
  $pb.BuilderInfo get info_ => _i;
  @$core.pragma('dart2js:noInline')
  static Report create() => Report._();
  Report createEmptyInstance() => create();
  static $pb.PbList<Report> createRepeated() => $pb.PbList<Report>();
  @$core.pragma('dart2js:noInline')
  static Report getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Report>(create);
  static Report? _defaultInstance;

  @$pb.TagNumber(1)
  $core.List<CompileUnit> get compileUnits => $_getList(0);

  @$pb.TagNumber(2)
  $core.int get fileTotal => $_getIZ(1);
  @$pb.TagNumber(2)
  set fileTotal($core.int v) {
    $_setUnsignedInt32(1, v);
  }

  @$pb.TagNumber(2)
  $core.bool hasFileTotal() => $_has(1);
  @$pb.TagNumber(2)
  void clearFileTotal() => clearField(2);

  @$pb.TagNumber(3)
  $core.int get vmTotal => $_getIZ(2);
  @$pb.TagNumber(3)
  set vmTotal($core.int v) {
    $_setUnsignedInt32(2, v);
  }

  @$pb.TagNumber(3)
  $core.bool hasVmTotal() => $_has(2);
  @$pb.TagNumber(3)
  void clearVmTotal() => clearField(3);
}
