// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// ignore_for_file: implementation_imports

import 'dart:async';
import 'dart:typed_data';

import 'package:fidl/fidl.dart';
import 'package:fidl_test_inspect_validate/fidl_async.dart' as fidl_validate;
import 'package:fuchsia_inspect/src/inspect/inspect.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
//import 'package:fuchsia/fuchsia.dart' as fuchsia; // for fuchsia.exit()

class _ValidateImpl extends fidl_validate.Validate {
  final _binding = fidl_validate.ValidateBinding();
  Inspect _inspect;
  final _nodes = <int, Node>{};
  final _properties = <int, Property>{};

  void bind(InterfaceRequest<fidl_validate.Validate> request) {
    _binding.bind(this, request);
  }

  @override
  Future<fidl_validate.Validate$Initialize$Response> initialize(
      fidl_validate.InitializationParams params) async {
    _inspect = Inspect();

    var handle = _inspect.vmoHandleForExportTestOnly;
    return fidl_validate.Validate$Initialize$Response(
        handle, fidl_validate.TestResult.ok);
  }

  @override
  Future<fidl_validate.Validate$InitializeTree$Response> initializeTree(
      fidl_validate.InitializationParams params) async {
    return fidl_validate.Validate$InitializeTree$Response(
        null, fidl_validate.TestResult.unimplemented);
  }

  @override
  Future<fidl_validate.TestResult> publish() async {
    // The inspect file is published by default, we do not need to publish it again.
    return fidl_validate.TestResult.ok;
  }

  @override
  Future<fidl_validate.TestResult> unpublish() async {
    return fidl_validate.TestResult.ok;
  }

  @override
  Future<fidl_validate.TestResult> act(fidl_validate.Action action) async {
    if (_inspect == null) {
      return fidl_validate.TestResult.illegal;
    }
    switch (action.$tag) {
      case fidl_validate.ActionTag.createNode:
        _nodes[action.createNode.id] =
            lookupNode(action.createNode.parent).child(action.createNode.name);
        break;
      case fidl_validate.ActionTag.deleteNode:
        _nodes.remove(action.deleteNode.id).delete();
        break;
      case fidl_validate.ActionTag.createNumericProperty:
        switch (action.createNumericProperty.value.$tag) {
          case fidl_validate.NumberTag.intT:
            final property = lookupNode(action.createNumericProperty.parent)
                .intProperty(action.createNumericProperty.name)
              ..setValue(action.createNumericProperty.value.intT);
            _properties[action.createNumericProperty.id] = property;
            break;
          case fidl_validate.NumberTag.doubleT:
            final property = lookupNode(action.createNumericProperty.parent)
                .doubleProperty(action.createNumericProperty.name)
              ..setValue(action.createNumericProperty.value.doubleT);
            _properties[action.createNumericProperty.id] = property;
            break;
          default:
            return fidl_validate.TestResult.unimplemented;
        }
        break;
      case fidl_validate.ActionTag.createStringProperty:
        final property = lookupNode(action.createStringProperty.parent)
            .stringProperty(action.createStringProperty.name)
          ..setValue(action.createStringProperty.value);
        _properties[action.createStringProperty.id] = property;
        break;
      case fidl_validate.ActionTag.createBytesProperty:
        final valueAsByteData = ByteData.view(
            action.createBytesProperty.value.buffer,
            action.createBytesProperty.value.offsetInBytes,
            action.createBytesProperty.value.lengthInBytes);
        final property = lookupNode(action.createBytesProperty.parent)
            .byteDataProperty(action.createBytesProperty.name)
          ..setValue(valueAsByteData);
        _properties[action.createBytesProperty.id] = property;
        break;
      case fidl_validate.ActionTag.createBoolProperty:
        final property = lookupNode(action.createBoolProperty.parent)
            .boolProperty(action.createBoolProperty.name)
          ..setValue(action.createBoolProperty.value);
        _properties[action.createBoolProperty.id] = property;
        break;
      case fidl_validate.ActionTag.deleteProperty:
        _properties.remove(action.deleteProperty.id).delete();
        break;
      case fidl_validate.ActionTag.addNumber:
        switch (action.addNumber.value.$tag) {
          case fidl_validate.NumberTag.intT:
            IntProperty p = _properties[action.addNumber.id];
            p.add(action.addNumber.value.intT);
            break;
          case fidl_validate.NumberTag.doubleT:
            DoubleProperty p = _properties[action.addNumber.id];
            p.add(action.addNumber.value.doubleT);
            break;
          default:
            return fidl_validate.TestResult.unimplemented;
        }
        break;
      case fidl_validate.ActionTag.subtractNumber:
        switch (action.subtractNumber.value.$tag) {
          case fidl_validate.NumberTag.intT:
            IntProperty p = _properties[action.subtractNumber.id];
            p.subtract(action.subtractNumber.value.intT);
            break;
          case fidl_validate.NumberTag.doubleT:
            DoubleProperty p = _properties[action.subtractNumber.id];
            p.subtract(action.subtractNumber.value.doubleT);
            break;
          default:
            return fidl_validate.TestResult.unimplemented;
        }
        break;
      case fidl_validate.ActionTag.setNumber:
        switch (action.setNumber.value.$tag) {
          case fidl_validate.NumberTag.intT:
            IntProperty p = _properties[action.setNumber.id];
            p.setValue(action.setNumber.value.intT);
            break;
          case fidl_validate.NumberTag.doubleT:
            DoubleProperty p = _properties[action.setNumber.id];
            p.setValue(action.setNumber.value.doubleT);
            break;
          default:
            return fidl_validate.TestResult.unimplemented;
        }
        break;
      case fidl_validate.ActionTag.setBytes:
        final valueAsByteData = ByteData.view(
            action.setBytes.value.buffer,
            action.setBytes.value.offsetInBytes,
            action.setBytes.value.lengthInBytes);
        BytesProperty p = _properties[action.setBytes.id];
        p.setValue(valueAsByteData);
        break;
      case fidl_validate.ActionTag.setString:
        StringProperty p = _properties[action.setString.id];
        p.setValue(action.setString.value);
        break;
      case fidl_validate.ActionTag.setBool:
        BoolProperty p = _properties[action.setBool.id];
        p.setValue(action.setBool.value);
        break;
      default:
        return fidl_validate.TestResult.unimplemented;
    }
    return fidl_validate.TestResult.ok;
  }

  @override
  Future<fidl_validate.TestResult> actLazy(
      fidl_validate.LazyAction action) async {
    return fidl_validate.TestResult.unimplemented;
  }

  Node lookupNode(int id) {
    return (id == 0) ? _inspect.root : _nodes[id];
  }
}

void main(List<String> args) {
  final context = ComponentContext.create();
  setupLogger();
  // Initialize & serve the inspect singleton before use in _ValidateImpl.
  Inspect().serve(context.outgoing);
  final validate = _ValidateImpl();

  context.outgoing
    ..addPublicService<fidl_validate.Validate>(
        validate.bind, fidl_validate.Validate.$serviceName)
    ..serveFromStartupInfo();
}
