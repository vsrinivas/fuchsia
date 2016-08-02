// Copyright 2016 The Fuchsia Authors. All rights reserved.
'use strict';

/**
 * Given a parsed JSON structure from the inspector server's Session endpoint
 * return an array of module objects that can be used to construct various
 * visualizations of the dataflow.
 */
function moduleDataflow(sessionJSON) {
  function intersection(s1, s2) {
    let s = new Set();
    s1.forEach((item) => {
      if (s2.has(item)) {
        s.add(item);
      }
    });
    return s;
  }

  let abbreviateUrl = (url) =>
      url ? url.replace(/.*\//, '').replace(/\..*/, '') : null;

  class Module {
    constructor(moduleJSON) {
      this.id = moduleJSON.id;
      this.url = moduleJSON.url;
      this.verb = moduleJSON.verb;
      this.label = abbreviateUrl(moduleJSON.url) || moduleJSON.verb;

      function moduleIOsToMap(ios, condition) {
        let map = new Map();
        ios.forEach((io) => {
          if (!condition(io)) {
            return;
          }
          let ids = new Set();
          io.matches.forEach((match) => {
            match.edges.forEach((edge) => {
              ids.add(edge.target);
            });
          });
          map.set(io.pathExpr, ids);
        });
        return map;
      }
      let inputExprs = new Set(moduleJSON.inputExprs);
      let outputExprs = new Set(moduleJSON.outputExprs);
      let composeExprs = new Set(moduleJSON.composeExprs);
      let displayExprs = new Set(moduleJSON.displayExprs);
      this.inputs = moduleIOsToMap(moduleJSON.inputs,
        (i) => inputExprs.has(i.pathExpr));
      this.composes = moduleIOsToMap(moduleJSON.inputs,
        (c) => composeExprs.has(c.pathExpr));
      this.outputs = moduleIOsToMap(moduleJSON.outputs,
        (o) => outputExprs.has(o.pathExpr));
      this.displays = moduleIOsToMap(moduleJSON.outputs,
        (d) => displayExprs.has(d.pathExpr));

      this.instances = moduleJSON.instances
          .filter((i) => i)
          .map((i) => new Instance(i, this));
    }
  }

  class Instance {
    constructor(instanceJSON, module) {
      this.id = instanceJSON.id;
      this.module = module;
      this.label = `${module.label}#${this.id}`;

      // Organize input matches into a map of path expression to set of
      // node ids.
      let inputMatches = new Map();
      instanceJSON.inputMatches.forEach((match) => {
        inputMatches.set(match.pathExpr, new Set(match.edges.map((edge) => edge.target)));
      });

      this.inputs = new Map();
      module.inputs.forEach((ids, pathExpr) => {
        this.inputs.set(pathExpr, intersection(ids, inputMatches.get(pathExpr)));
      });

      this.composes = new Map();
      module.composes.forEach((ids, pathExpr) => {
        this.composes.set(pathExpr, intersection(ids, inputMatches.get(pathExpr)));
      });

      // Which of the module's outputs/displays were produced by this
      // instance?
      let outputNodes = new Set(instanceJSON.outputEdges.map((edge) => edge.target));

      this.outputs = new Map();
      module.outputs.forEach((ids, pathExpr) => {
        this.outputs.set(pathExpr, intersection(ids, outputNodes));
      });

      this.displays = new Map();
      module.displays.forEach((ids, pathExpr) => {
        this.displays.set(pathExpr, intersection(ids, outputNodes));
      });
    }
  }

  return sessionJSON.modules.map((j) => new Module(j));
}
