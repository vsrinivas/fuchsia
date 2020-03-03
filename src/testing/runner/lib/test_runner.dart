// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';
import 'package:args/args.dart';
import 'package:json_schema/json_schema.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const String planSchemaString = r'''
{
  "definitions": {
    "name": { "type": "string"},
    "test": {
      "oneOf": [
        {
          "$ref": "#/definitions/component_url"
        },
        {
          "type": "object",
          "properties": {
            "component_url": { "$ref": "#/definitions/component_url" },
            "environments": {
              "type": "array",
              "items": { "$ref": "#/definitions/environment" }
            },
            "predicates": {
              "type": "array",
              "items": { "$ref": "#/definitions/predicate" }
            }
          },
          "required": [ "component_url" ]
        }
      ]
    },
    "component_url": {
      "type": "string",
      "format": "uri"
    },
    "environment": {
      "type": "object",
      "properties": {
        "dimensions": {
          "type": "object"
        },
        "tags": {
          "type": "array",
          "items": { "type": "string" }
        }
      }
    },
    "predicate": {
      "type": "object",
      "properties": {
        "name": { "type": "string" },
        "category": {
          "type": "string",
          "enum": [ "tiny", "small", "medium", "large" ]
        }
      }
    }
  },
  "type": "object",
  "properties": {
    "tests": {
      "type": "array",
      "items": {
        "$ref": "#/definitions/test"
      }
    }
  }
}
''';

void main(List<String> args) async {
  ArgParser parser = ArgParser()..addOption('plan', abbr: 'p');
  ArgResults parsedArgs = parser.parse(args);
  String planPath = parsedArgs['plan'];
  final plan = json.decode(await File(planPath).readAsString());

  final Schema planSchema =
      await Schema.createSchema(json.decode(planSchemaString));

  final Validator validator = Validator(planSchema);

  if (!validator.validate(plan)) {
    print('Plan did not pass validation: ${validator.errors}');
    return;
  }

  print('creating sl4f driver');
  sl4f.Sl4f sl4fDriver = sl4f.Sl4f.fromEnvironment();
  print('starting sl4f driver');
  await sl4fDriver.startServer();
  print('creating test driver');
  sl4f.Test testDriver = sl4f.Test(sl4fDriver);

  bool allPassed = true;

  print('executing plan: $plan');
  for (var test in plan['tests']) {
    print('Running suite $test.. ');
    final result = await testDriver.runTest(test);
    List<sl4f.TestStep> steps = result.steps;
    for (sl4f.TestStep step in steps) {
      print('  ${step.name}: ${step.outcome}');
      if (step.outcome != 'passed') {
        allPassed = false;
      }
    }
    print('Outcome: ${result.outcome}');
    if (!result.successful_completion) {
      print('test did not complete successfully.');
      exitCode = 1;
    }
  }

  await sl4fDriver.stopServer();

  if (allPassed) {
    print('All tests passed!');
  }
  if (!allPassed) {
    print('Some tests failed');
    exitCode = 1;
  }
}
