# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import collections
import glob
import os
import shutil
import subprocess
import sys
import tempfile

from pylib import args_lib

ArchTuple = collections.namedtuple('ArchTuple', ['arg', 'cs_path'])


# An dictionary that allows to access element as members.
class AttrDict(dict):
  def __getattr__(self, attr):
    return self.get(attr, None)

  def __setattr__(self, attr, value):
    self[attr] = value


def _generate_index(modular_root_dir, deploy_dir):
  dart_binary = os.path.join(modular_root_dir,
        'third_party', 'flutter', 'bin', 'cache', 'dart-sdk', 'bin', 'dart')
  indexer_tool = os.path.join(modular_root_dir,
                              'indexer', 'pipeline', 'bin', 'run.dart')
  examples_dir = os.path.join(modular_root_dir, 'examples')
  subprocess.check_call([dart_binary, indexer_tool,
                         '--host=https://tq.mojoapps.io/',
                         '--host-root=%s' % modular_root_dir,
                         '--output-directory=%s' % deploy_dir,
                         examples_dir])


def _replace_content_handler(filename, old_content_handler,
                             new_content_handler):
  with open(filename, 'r') as f:
    contents = f.readlines()
  contents[0] = contents[0].replace(old_content_handler, new_content_handler)
  with open(filename, 'w') as f:
    f.writelines(contents)


def _insert_flutter_url(flx_file):
  """Inserts the flutter engine URL into the #! line of an flx file.

  During local development, flx files load with "mojo:flutter", and tools
  rely on this for passing arguments, etc. However, for things to work on the
  CDN, we must replace this with the actual, versioned URL.
  """
  _replace_content_handler(
      flx_file, "mojo:flutter", "https://tq.mojoapps.io/flutter.mojo")


def _insert_dart_content_handler_url(arch, modular_root_dir, mojo_file):
  """Inserts the dart content handler URL into the #! line of a dart snapshot.

  During local development, dart snapshot files load with
  "mojo:dart_content_handler", and tools rely on this for passing arguments,
  etc. However, for things to work on the CDN, we must replace this with the
  actual, versioned URL.
  """
  revision_file = os.path.join(modular_root_dir, 'MOJO_VERSION')
  with open(revision_file, 'r') as f:
    revision = f.read()
  _replace_content_handler(mojo_file, "mojo:dart_content_handler",
                           "https://storage.googleapis.com/mojo/services/%s/%s/"
                           "dart_content_handler.mojo"
                           % (arch.cs_path, revision))

def _deploy_assets(out_dir, deploy_dir, app):
  assert app.endswith('.mojo') or app.endswith('.flx')
  # We obtain the app deployment directory from the name of the flx/mojo file.
  # TODO(armansito): Change this so that the flx/mojo file is a sibling of the
  # deployed assets.
  app_dir = os.path.basename(app).split('.')[0]
  assets = glob.glob(os.path.join(out_dir, '%s/*' % app_dir))
  for asset in assets:
    dest_dir = os.path.dirname(asset.replace(out_dir, deploy_dir))
    if not os.path.exists(dest_dir):
      os.makedirs(dest_dir)
    shutil.copy(asset, dest_dir)


def _populate_deploy_dir(arch, args, deploy_dir):
  _generate_index(args.modular_root_dir, deploy_dir)

  mojo_apps = glob.glob(os.path.join(args.out_dir, "*.mojo"))
  for mojo_app in mojo_apps:
    _insert_dart_content_handler_url(arch, args.modular_root_dir, mojo_app)
    _deploy_assets(args.out_dir, deploy_dir, mojo_app)

  flutter_apps = glob.glob(os.path.join(args.out_dir, "*.flx"))
  for flutter_app in flutter_apps:
    _insert_flutter_url(flutter_app)
    _deploy_assets(args.out_dir, deploy_dir, flutter_app)

  flutter_content_handler = os.path.join(args.modular_root_dir, 'third_party',
                                         'flutter', 'bin', 'cache', 'artifacts',
                                         'engine', arch.cs_path, 'flutter.mojo')

  apps = []
  apps.extend(mojo_apps)
  apps.extend(flutter_apps)
  apps.append(flutter_content_handler)
  for app in apps:
    shutil.copy(app, deploy_dir)

  from_dir = os.path.join(args.modular_root_dir, 'examples')
  to_dir = os.path.join(deploy_dir, 'examples')

  for root, _, files in os.walk(from_dir):
    if '/packages' in root or '/.pub' in root:
      continue
    for f in files:
      # We deploy all YAML files since we need to deploy recipes and manifests.
      # pubspec.yaml and assets.yaml are collateral damage.
      if not f.endswith(".yaml"):
        continue
      src_file = os.path.join(root, f)
      dest_dir = os.path.dirname(src_file.replace(from_dir, to_dir))
      if not os.path.exists(dest_dir):
        os.makedirs(dest_dir)
      shutil.copy(src_file, dest_dir)


def _check_source_tree(args, revision):
  # Check that the current branch is pushed remotely.
  synced_remote_branches = subprocess.check_output(["git",
                                                    "branch",
                                                    "-r",
                                                    "--contains",
                                                    revision]).strip()
  if not synced_remote_branches:
    print "Current revision is not pushed to remote branch. Bailing out."
    return False
  # Check that the source directory is unmodified.
  local_modifications = subprocess.check_output(["git",
                                                 "-C",
                                                 args.modular_root_dir,
                                                 "status",
                                                 "--porcelain"])
  if local_modifications.strip():
    print ("The source tree contains local modification:\n%sBailing out." %
           local_modifications)
    return False
  # Check that the output directory exists.
  if not os.path.exists(args.out_dir):
    print "Output directory: %s does not exist. Bailing out." % args.out_dir
    return False

  return True


def deploy(global_args):
  if not global_args.release:
    print "Deploying debug builds is not supported. Bailing out."
    return 0

  if global_args.target == 'android':
    arch = ArchTuple('android', 'android-arm')
  else:
    arch = ArchTuple(None, 'linux-x64')

  gsutil_exe = os.path.join(global_args.modular_root_dir, "../gsutil/gsutil")
  if not os.path.isfile(gsutil_exe):
    print "Cannot find gsutil."
    return 1

  revision = subprocess.check_output(['git',
                                      '-C',
                                      global_args.modular_root_dir,
                                      'rev-parse',
                                      'HEAD']).strip()

  args_per_arch = {}
  args = AttrDict()
  args['release'] = True
  if arch.arg:
    args['target'] = arch.arg
  args_lib.parse_common_args(args)
  args_per_arch[arch] = args
  if not _check_source_tree(args, revision):
    if global_args.local_deploy_dir:
      print "As this is local deployment, continuing despite bad tree state."
    else:
      print "Cannot deploy to CDN in bad tree state."
      return 1

  working_dir = tempfile.mktemp()
  os.makedirs(working_dir)
  num_files = 0
  try:
    for arch, args in args_per_arch.items():
      deployment_dir = os.path.join(
          working_dir, 'services', arch.cs_path, revision)
      os.makedirs(deployment_dir)
      _populate_deploy_dir(arch, args, deployment_dir)

      # index.html is special on the CDN. It must exist at root rather than in
      # the deploy dir. So we copy it there.
      if global_args.stable:
        index_html_file = os.path.join(deployment_dir, 'index.html')
        assert os.path.exists(index_html_file)
        shutil.copy(index_html_file, working_dir)

      for root, _, files in os.walk(deployment_dir):
        rel = os.path.relpath(root, deployment_dir)
        target_location_dir = os.path.join(
            working_dir, 'services', arch.cs_path, rel)
        if not os.path.exists(target_location_dir):
          os.makedirs(target_location_dir)
        location_content_dir = (
            'modular/services/%s/%s' % (arch.cs_path, revision))

        for f in files:
          num_files += 1
          if global_args.stable:
            relative_path = os.path.normpath(os.path.join(rel, f))
            with open(os.path.join(target_location_dir, '%s_location' % f),
                      'w') as l:
              l.write('%s/%s' % (location_content_dir, relative_path))

    assert num_files < 1024,\
        'Tried to deploy a suspiciously large number of files (%d).' % num_files
    print 'Found %d files to deploy' % num_files

    for sub in os.listdir(working_dir):
      source = os.path.join(working_dir, sub)
      print 'Deploying', source

      if global_args.local_deploy_dir:
        dest = os.path.join(global_args.local_deploy_dir, sub)
        if os.path.isdir(source):
          shutil.copytree(source, dest)
        else:
          shutil.copyfile(source, dest)
      else:
        subprocess.check_call([
            gsutil_exe, '-m', 'cp', '-r', source, 'gs://modular'],
            stdout=global_args.debug_only_pipe,
            stderr=global_args.debug_only_pipe)

  finally:
    shutil.rmtree(working_dir)

  return 0
