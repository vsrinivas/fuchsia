// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Utilities for manipulating and introspecting into tasks.
 */

fidl.loadLibrary('fuchsia.kernel');

/**
 * Walks the tree of tasks (excluding threads) from a given zx.Job.
 *
 * @param {zx.Job} job The job from which to walk the tree.
 * @param {callback} preCallback Before iterating over the children of a zx.Task, is passed that
 *     zx.Task
 * @param {callback} postCallback After iterating over the children of a zx.Task, is passed that
 *     zx.Task
 */
function walkJobTree(job, preCallback, postCallback) {
  preCallback(job);
  let childrenKoids = job.getChildrenInfo();
  for (let i = 0; i < childrenKoids.length; i++) {
    let childHandle = job.getChild(childrenKoids[i], zx.ZX_RIGHT_SAME_RIGHTS);
    walkJobTree(new zx.Job(childHandle), preCallback, postCallback);
  }
  let processesKoids = job.getProcessesInfo();
  for (let i = 0; i < processesKoids.length; i++) {
    let processHandle = job.getChild(processesKoids[i], zx.ZX_RIGHT_SAME_RIGHTS);
    let process = new zx.Process(processHandle);
    preCallback(process);
    postCallback(process);
  }
  postCallback(job);
}

/**
 * Represents metadata about a given task.
 */
class TaskInfo {
  constructor(task) {
    this.info = task.getBasicInfo();
    this.name = task.getProperty(zx.ZX_PROP_NAME);
  }
}

/**
 * Pretty prints a process tree to stdout.
 *
 * @param {Number} level How many spaces to print at the beginning of the first task.
 * @param {TaskInfo} rootObject Info about the root-level task
 * @param {Map} map Map from TaskInfo -> TaskInfo[] children.
 */
function printProcessMap(level, rootObject, map) {
  let s = '';
  for (let j = 0; j < level; j++) {
    s += ' ';
  }
  s += rootObject.info.koid + ' ' + rootObject.name + '\n';
  const children = map.get(rootObject);
  if (children != undefined) {
    for (let i = 0; i < children.length; i++) {
      s += printProcessMap(level + 1, children[i], map);
    }
  }
  return s;
}

function psJob(rootJob) {
  // A map from TaskInfo to a list of child TaskInfo objects.
  let jobMap = new Map();
  // A stack of objects, maintained as we walk the job tree.
  let currObject = [];
  // The root object.  An array so we can modify it in the callback.
  let rootObject = [];
  walkJobTree(
      rootJob,
      (object) => {
        let taskInfo = new TaskInfo(object);
        if (currObject.length == 0) {
          rootObject.push(taskInfo);
        } else {
          let val = jobMap.get(currObject[currObject.length - 1]);
          val.push(taskInfo);
          jobMap.set(currObject[currObject.length - 1], val);
        }
        currObject.push(taskInfo);
        jobMap.set(currObject[currObject.length - 1], []);
      },
      (object) => {
        currObject.pop();
        object.close();
      });
  jobMap.toString = () => {
    return printProcessMap(0, rootObject[0], jobMap);
  };
  jobMap.root = rootObject[0];
  return jobMap;
}

/**
 * Returns Map from TaskInfo to TaskInfo[], where the key is a given task,
 * and the value is its children.  The root task is in the map with value root.
 */
async function ps() {
  let rootJobResult = await svc.fuchsia_kernel_RootJob.Get();
  return psJob(rootJobResult['job']);
}

/**
 * Throws an error if no task had the given koid
 * @param {Number} taskToKillId the task koid
 */
async function kill(taskToKillId) {
  let rootJobResult = await svc.fuchsia_kernel_RootJob.Get();
  // handle to the task to kill. An array so we can modify it in the callback.
  let killedTask = false;
  walkJobTree(rootJobResult['job'], (o) => {}, (task) => {
    if (task.getBasicInfo().koid == taskToKillId) {
      task.kill();
      killedTask = true;
    }
  });
  if (!killedTask) {
    throw 'No task with given id';
  }
}

/**
 * Throws an error if no task matched the given name or regex
 * @param {String} taskName task name, will be interpreted as a regex if "r" is in the options
 * @param {String} options defaults to empty string, can be 'r' to interpret task name as regex
 */
async function killall(taskName, options = '') {
  let rootJobResult = await svc.fuchsia_kernel_RootJob.Get();
  // handle to the task to kill. An array so we can modify it in the callback.
  let killedTask = false;
  let matchAsRegex = options.includes('r');
  walkJobTree(rootJobResult['job'], (o) => {}, (task) => {
    if (matchAsRegex && task.getProperty(zx.ZX_PROP_NAME).match(taskName)) {
      task.kill();
      killedTask = true;
    } else if (!matchAsRegex && task.getProperty(zx.ZX_PROP_NAME) == taskName) {
      task.kill();
      killedTask = true;
    }
  });
  if (!killedTask) {
    throw ('No task with given id');
  }
}

export {ps, psJob, kill, killall};
