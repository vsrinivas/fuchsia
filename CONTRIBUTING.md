Contributing Changes
====================

Fuchsia manages commits through Gerrit at
https://fuchsia-review.googlesource.com. Not all projects accept patches;
please see the CONTRIBUTING.md document in individual projects for
details.

## Submitting changes

To submit a patch to Fuchsia, you may first need to generate a cookie to
authenticate you to Gerrit. To generate a cookie, log into Gerrit and click
the "Generate Password" link at the top of https://fuchsia.googlesource.com.
Then, copy the generated text and execute it in a terminal.

Once authenticated, follow these steps to submit a patch to a repo in Fuchsia:

```
# create a new branch
git checkout -b branch_name

# write some awesome stuff, commit to branch_name
# edit some_file ...
git add some_file
# if specified in the repo, follow the commit message format
git commit ...

# upload the patch to Gerrit
# `jiri help upload` lists flags for various features, e.g. adding reviewers
jiri upload # Adds default topic - ${USER}-branch_name
# or
jiri upload -topic="custom_topic"
# or
git push origin HEAD:refs/for/master

# at any time, if you'd like to make changes to your patch, use --amend
git commit --amend

# once the change is landed, clean up the branch
git branch -d branch_name
```

See the Gerrit documentation for more detail:
[https://gerrit-documentation.storage.googleapis.com/Documentation/2.12.3/intro-user.html#upload-change](https://gerrit-documentation.storage.googleapis.com/Documentation/2.12.3/intro-user.html#upload-change)

## [Non-Googlers only] Sign the Google CLA

In order to land your change, you need to sign the [Google CLA](https://cla.developers.google.com/).

## [Googlers only] Issue actions

Commit messages may reference issue IDs in Fuchsia's
[issue tracker](https://fuchsia.atlassian.net/); such references will become
links in the Gerrit UI. Issue actions may also be specified, for example to
automatically close an issue when a commit is landed:

BUG-123 #done

`done` is the most common issue action, though any workflow action can be
indicated in this way.

Issue actions take place when the relevant commit becomes visible in a Gerrit
branch, with the exception that commits under refs/changes/ are ignored.
Usually, this means the action will happen when the commit is merged to
master, but note that it will also happen if a change is uploaded to a private
branch.

*Note*: Fuchsia's issue tracker is not open to external contributors at this
time.

## Switching between layers

When you bootstrapped your development environment (see
[getting source](getting_source.md)), you selected a layer. Your development
enviornment views that layer at the latest revision and views the lower layers
at specific revisions in the past.

If you want to switch to working on a different layer, either to get the source
code for higher layers in your source tree or to see lower layers at more recent
revisions, you have two choices:

1. You can bootstrap a new development environment for that layer using
   [the same instructions you used originally](getting_source.md).
2. You can modify your existing development environment using the
   `fx set-layer <layer>` command. This command edits the `jiri` metadata for
   your source tree to refer to the new layer and prints instructions for how to
   actually get the source and build the newly configured layer.

## Changes that span layers

Fuchsia is divided into a number of [layers](layers.md). Each layer views the
previous layers at pinned revisions, which means changes that land in one layer
are not immediately visible to the upper layers.

When making a change that spans layers, you need to think about when the
differnet layers will see the different parts of you change. For example,
suppose you want to change an interface in Zircon and affects clients in Garnet.
When you land your change in Zircon, people building Garnet will not see your
change immediately. Instead, they will start seeing your change once Garnet
updates its revision pin for Zircon.

### Soft transitions (preferred)

The preferred way to make changes that span multiple layers is to use a
*soft transition*. In a soft transition, you make a change to the lower layer
(e.g., Zircon) in such a way that the interface supports both old and new
clients. For example, if you are replacing a function, you might add the new
version and turn the old function into a wrapper for the new function.

Use the follow steps to land a soft transition:

1. Land the change in the lower layer (e.g., Zircon) that introduces the new
   interface without breaking the old interface used by the upper layer
   (e.g., Garnet).
2. Wait for the autoroll bot to update the revision of the lower layer
   used by the upper layer.
3. Land the change to the upper layer that migrates to the new interface.
4. Land a cleanup change in the lower layer that removes the old interface.

### Hard transitions

For some changes, creating a soft transition can be difficult or impossible. For
those changes, you can make a *hard transition*. In a hard transition, you make
a breaking change to the lower layer and update the upper layer manually.

Use the follow steps to land a hard transition:

1. Land the change in the lower layer (e.g., Zircon) that breaks the interface
   used by the upper layer (e.g., Garnet). At this point, the autoroll bot will
   start failing to update the upper layer.
3. Land the change to the upper layer that both migrates to the new interface
   and updates the revision of the lower layer used by the upper layer by
   editing the `revision` attribute for the import of the lower layer in the
   `//<layer>/manifest/<layer>` manifest of the upper layer.

Making a hard transition is more stressful than making a soft transition because
your change will be preventing other changes in the lower layer from becoming
available in the upper layers between steps 1 and 2.

## Cross-repo changes

Changes in two or more separate repos will be automatically tracked for you by
Gerrit if you use the same topic.

### Using jiri upload
Create branch with same name on all repos and upload the changes
```
# make and commit the first change
cd fuchsia/bin/fortune
git checkout -b add_feature_foo
* edit foo_related_files ... *
git add foo_related_files ...
git commit ...

# make and commit the second change in another repository
cd fuchsia/build
git checkout -b add_feature_foo
* edit more_foo_related_files ... *
git add more_foo_related_files ...
git commit ...

# Upload all changes with the same branch name across repos
jiri upload -multipart # Adds default topic - ${USER}-branch_name
# or
jiri upload -multipart -topic="custom_topic"

# after the changes are reviewed, approved and submitted, clean up the local branch
cd fuchsia/bin/fortune
git branch -d add_feature_foo

cd fuchsia/build
git branch -d add_feature_foo
```

### Using Gerrit commands

```
# make and commit the first change, upload it with topic 'add_feature_foo'
cd fuchsia/bin/fortune
git checkout -b add_feature_foo
* edit foo_related_files ... *
git add foo_related_files ...
git commit ...
git push origin HEAD:refs/for/master%topic=add_feature_foo

# make and commit the second change in another repository
cd fuchsia/build
git checkout -b add_feature_foo
* edit more_foo_related_files ... *
git add more_foo_related_files ...
git commit ...
git push origin HEAD:refs/for/master%topic=add_feature_foo

# after the changes are reviewed, approved and submitted, clean up the local branch
cd fuchsia/bin/fortune
git branch -d add_feature_foo

cd fuchsia/build
git branch -d add_feature_foo
```

Multipart changes are tracked in Gerrit via topics, will be tested together,
and can be landed in Gerrit at the same time with `Submit Whole Topic`. Topics
can be edited via the web UI.

## Resolving merge conflicts

```
# rebase from origin/master, revealing the merge conflict
git rebase origin/master

# resolve the conflicts and complete the rebase
* edit files_with_conflicts ... *
git add files_with_resolved_conflicts ...
git rebase --continue
jiri upload

# continue as usual
git commit --amend
jiri upload
```

## Github integration

While Fuchsia's code is hosted at https://fuchsia.googlesource.com, it is also
mirrored to https://github.com/fuchsia-mirror. To ensure Fuchsia contributions
are associated with your Github account:

1. [Set your email in Git](https://help.github.com/articles/setting-your-email-in-git/).
2. [Adding your email address to your GitHub account](https://help.github.com/articles/adding-an-email-address-to-your-github-account/).
3. Star the project for your contributions to show up in your profile's
Contribution Activity.
