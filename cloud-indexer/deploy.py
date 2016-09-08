#!/usr/bin/python

# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Deploy script for cloud-indexer."""

import sys

import abc
import argparse
import distutils.spawn
import logging
import os
import re
import shutil
import subprocess
import tempfile

DEFAULT_INDEXER_BUCKET_NAME = 'modular-cloud-indexer.google.com.a.appspot.com'

# TODO(victorkwan): Switch this out to 'modular' once we're ready to deploy.
DEFAULT_MODULE_BUCKET_NAME = 'modular-cloud-indexer.google.com.a.appspot.com'
DEFAULT_TOPIC_NAME = 'projects/google.com:modular-cloud-indexer/topics/indexing'
TARGET_CHOICES = ('default', 'notification-handler', 'dispatch')

# We assume that the deployment script is in the root of the cloud-indexer dir.
SCRIPT_PATH = os.path.abspath(__file__)
CLOUD_INDEXER_PATH = os.path.dirname(SCRIPT_PATH)

# Locate our bundled dependencies.
MODULAR_PATH = os.path.abspath(os.path.join(CLOUD_INDEXER_PATH, '..'))
FLUTTER_PATH = os.path.join(MODULAR_PATH, 'third_party', 'flutter')
DART_SDK_PATH = os.path.join(FLUTTER_PATH, 'bin', 'cache', 'dart-sdk')
PUB_PATH = os.path.join(DART_SDK_PATH, 'bin', 'pub')

# Finally, we assume that gcloud is installed and in the PATH variable.
GCLOUD_PATH = distutils.spawn.find_executable('gcloud')


def copy_and_overwrite(source, destination, ignore=None):
  """Recursively copies the source directory to the destination directory."""
  # shutil.copytree throws an exception if the destination directory already
  # exists. So, we remove the directory before trying to copy.
  if os.path.exists(destination):
    shutil.rmtree(destination)
  shutil.copytree(source, destination, ignore=ignore)


class DeployCommand(object):
  """Abstract command that can be executed using the DeployCommandRunner."""

  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def setup(self, env_vars, deploy_path):
    """Populates deploy_path with the necessary files for deployment.

    Args:
      env_vars: A dictionary containing environment variables.
      deploy_path: The root of the deploy directory.

    Returns:
      True on success, otherwise returns False.
    """
    pass

  @abc.abstractproperty
  def config_path(self):
    """The path of the config file relative to the deployment directory."""
    pass


class DartManagedVMModuleCommand(DeployCommand):
  """Command that deploys the a Dart Managed VM module."""

  # Base files to be ignored during copying.
  COPYTREE_IGNORES = ['Dockerfile', '.*', 'packages']

  def __init__(self, service_name, config_name):
    self.service_name = service_name
    self.config_name = config_name

    # We ignore the base ignores, as well as the config file.
    self.ignored_files = DartManagedVMModuleCommand.COPYTREE_IGNORES[:]
    self.ignored_files.append(config_name)

  def setup(self, env_vars, deploy_path):
    # We want to reorganize the service such that it can be deployed using the
    # image google/dart-runtime-base. More details can be found at
    # https://hub.docker.com/r/google/dart-runtime-base/.
    service_path = os.path.join(CLOUD_INDEXER_PATH, 'app', self.service_name)
    service_deploy_path = os.path.join(deploy_path, self.service_name)

    # Generate the lockfile for the local dependencies.
    logging.info('Running `pub get` in %s', self.service_name)
    p = subprocess.Popen([PUB_PATH, 'get'], cwd=service_path)
    p.communicate()

    if p.returncode != 0:
      logging.error('`pub get` in %s failed. Bailing out.', self.service_name)
      return False

    logging.info('Copying %s service files', self.service_name)
    service_app_deploy_path = os.path.join(service_deploy_path, 'app')
    copy_and_overwrite(service_path, service_app_deploy_path,
                       ignore=shutil.ignore_patterns(*self.ignored_files))

    # Like the Dockerfile, the service configuration file has to be at the root
    # of the service directory.
    logging.info('Copying %s', self.config_name)
    config_deploy_path = os.path.join(service_deploy_path, self.config_name)
    shutil.copyfile(os.path.join(service_path, self.config_name),
                    config_deploy_path)
    with open(config_deploy_path, 'a') as cf:
      cf.write('\n'.join([
          'env_variables:',
          '\n'.join(['  {}: \'{}\''.format(k, v)
                     for (k, v) in env_vars.items()]),
          ''
      ]))

    # Get all the local dependencies inside the lockfile.
    with open(os.path.join(service_path, 'pubspec.lock')) as lf:
      contents = lf.read()

    packages = {}
    dependencies = re.findall(r'path: "(../\S+)"', contents)
    for dependency in dependencies:
      source_path = os.path.abspath(os.path.join(service_path, dependency))

      logging.info('Copying dependency: %s', source_path)
      relative_path = os.path.relpath(source_path, MODULAR_PATH)
      destination_path = os.path.join(service_deploy_path, 'pkg', relative_path)
      copy_and_overwrite(source_path, destination_path,
                         ignore=shutil.ignore_patterns(*self.ignored_files))

      # We can cache this result for later, when we update pubspec.yaml.
      packages[dependency] = {
          'pubspec': os.path.relpath(destination_path,
                                     service_app_deploy_path),
          'dockerfile': os.path.join('pkg', relative_path)
      }

    def match_package(match_object):
      """Replacement function for relative paths in pubspec.yaml.

      Args:
        match_object: A match_object passed by re.sub containing a relative path
            from pubspec.yaml.

      Returns:
        A string corresponding to the relative path in the deployment directory.

      Raises:
        KeyError: if there is a mismatch between dependencies in
            pubspec.yaml and pubspec.lock, e.g. if pubspec.lock is out of date.
      """
      return 'path: {}/'.format(
          packages[match_object.group(1).rstrip('/')]['pubspec'])

    pubspec_path = os.path.join(service_app_deploy_path, 'pubspec.yaml')
    logging.info('Updating dependencies with relative paths')
    with open(pubspec_path, 'r+') as f:
      contents = f.read()
      f.seek(0)
      try:
        contents = re.sub(r'path: (../\S+)', match_package, contents)
      except KeyError:
        logging.error('Dependency mismatch in pubspec.yaml and pubspec.lock')
        return False
      f.write(contents)
      f.truncate()

    # Finally, we create the new Dockerfile based on the
    # google/dart-runtime-base container image.
    dockerfile_path = os.path.join(service_deploy_path, 'Dockerfile')
    logging.info('Writing Dockerfile')
    with open(dockerfile_path, 'w') as f:
      adds = ['ADD {} {}'.format(os.path.join(v['dockerfile'], 'pubspec.yaml'),
                                 '{}/'.format(os.path.join('/project',
                                                           v['dockerfile'])))
              for v in packages.values()]
      f.write('\n'.join([
          'FROM google/dart-runtime-base',
          'WORKDIR /project/app/',
          '\n'.join(adds),
          'ADD app/pubspec.* /project/app/',
          'RUN pub get',
          'ADD . /project/',
          'RUN pub get --offline',
          ''
      ]))

    return True

  @property
  def config_path(self):
    return os.path.join(self.service_name, self.config_name)


class DispatchConfigCommand(DeployCommand):
  """Deploys the dispatch.yaml configuration file."""

  def setup(self, env_vars, deploy_path):
    logging.info('Copying dispatch.yaml')
    dispatch_config_path = os.path.join(CLOUD_INDEXER_PATH, 'app',
                                        'dispatch.yaml')
    shutil.copy2(dispatch_config_path, deploy_path)
    return True

  @property
  def config_path(self):
    return 'dispatch.yaml'


class DeployCommandRunner(object):
  """Performs deployment, as specified by the added DeployCommand objects."""

  def __init__(self, env_vars, deploy_path, dry_run):
    self.env_vars = env_vars
    self.deploy_path = deploy_path
    self.dry_run = dry_run
    self.commands = []

  def add_command(self, command):
    self.commands.append(command)

  def run(self):
    """Performs deployment.

    Iterates through the added DeployCommand objects and calls the setup method
    to populate the deploy directory. Then, if dry_run is not True, invokes
    gcloud to deploy to the Google Cloud project.

    Returns:
      True if deployment is successful, otherwise False.
    """
    args = [GCLOUD_PATH, 'app', 'deploy']
    for command in self.commands:
      if not command.setup(self.env_vars, self.deploy_path):
        logging.error('Setup failed for command: %s', command.config_path)
        return False

      # Appends configuration file to be deployed using gcloud.
      args.append(command.config_path)

    if self.dry_run:
      logging.info('Dry run complete! The command we would have ran was:\n%s',
                   ' \\\n  '.join(args))
      return True

    p = subprocess.Popen(args, cwd=self.deploy_path)
    p.communicate()

    if p.returncode != 0:
      logging.error('`gcloud app deploy ...` failed. Bailing out.')
      return False

    return True


def main():
  logging.basicConfig(level=logging.INFO)
  parser = argparse.ArgumentParser(
      description='Deploys the cloud-indexer application.')
  parser.add_argument(
      '--dry-run',
      help=('Creates the deploy directory, but does not perform the deployment.'
            ' Use with --deploy-dir.'),
      action='store_true')
  parser.add_argument(
      '--topic-name',
      help=('Uses TOPIC_NAME as the topic used to communicate between services.'
            ' (default: %(default)s)'),
      action='store',
      default=DEFAULT_TOPIC_NAME)
  parser.add_argument(
      '--indexer-bucket-name',
      help=('Uses INDEXER_BUCKET_NAME as bucket to retrieve credentials from. '
            '(default: %(default)s)'),
      action='store',
      default=DEFAULT_INDEXER_BUCKET_NAME)
  parser.add_argument(
      '--module-bucket-name',
      help=('Uses MODULE_BUCKET_NAME as bucket to retrieve modules from. '
            '(default: %(default)s)'),
      action='store',
      default=DEFAULT_MODULE_BUCKET_NAME)
  parser.add_argument(
      '--deploy-dir',
      help=('Uses DEPLOY_DIR as the deployment directory.'),
      action='store')
  parser.add_argument(
      '--targets',
      help=('Deploys the specified targets. If none are specified, the script'
            ' deploys all the targets.'),
      action='store',
      nargs='*',
      choices=TARGET_CHOICES,
      default=TARGET_CHOICES)

  args = parser.parse_args()
  if args.deploy_dir is None and args.dry_run:
    logging.warning('deploy.py was invoked with --dry-run but into a temporary '
                    'directory. Use --deploy-dir to retain deploy output.')

  if args.deploy_dir is None:
    logging.info('Creating temporary directory')
    deploy_dir = tempfile.mkdtemp()
  else:
    deploy_dir = args.deploy_dir

  targets = set(args.targets)
  env_vars = {
      'INDEXER_BUCKET_NAME': args.indexer_bucket_name,
      'MODULE_BUCKET_NAME': args.module_bucket_name,
      'TOPIC_NAME': args.topic_name,
  }
  runner = DeployCommandRunner(env_vars, deploy_dir, args.dry_run)
  if 'default' in targets:
    runner.add_command(DartManagedVMModuleCommand('default', 'app.yaml'))
  if 'notification-handler' in targets:
    runner.add_command(DartManagedVMModuleCommand('notification-handler',
                                                  'notification-handler.yaml'))
  if 'dispatch' in targets:
    runner.add_command(DispatchConfigCommand())

  result = 1
  try:
    result = 0 if runner.run() else 1
    sys.exit(result)
  finally:
    if args.deploy_dir is None:
      # We use a finally block to guarantee the temporary folder is deleted.
      logging.info('Deleting the temporary directory')
      shutil.rmtree(deploy_dir)


if __name__ == '__main__':
  main()
