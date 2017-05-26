Contributing Changes
====================

Fuchsia manages commits through Gerrit at
https://fuchsia-review.googlesource.com. Not all projects accept patches;
please see the CONTRIBUTING.md document in individual projects for
details.

## Submitting changes

To submit a patch to Fuchsia, you may first need to generate a cookie to
authenticate you to Gerrit.  To generate a cookie, log into Gerrit and click
the "Generate Password" link at the top of https://fuchsia.googlesource.com.
Then, copy the generated text and execute it in a terminal.

Once authenticated, follow these steps to submit a patch to Fuchsia:

```
# create a new branch
git checkout -b branch_name

# write some awesome stuff, commit to branch_name
vim some_file ...
git commit ...

# upload the patch to gerrit
jiri upload # Adds default topic - ${USER}-add_feature_foo
# or
git push origin HEAD:refs/for/master

# once the change is landed, clean up the branch
git branch -d branch_name
```

See the Gerrit documentation for more detail:
[https://gerrit-documentation.storage.googleapis.com/Documentation/2.12.3/intro-user.html#upload-change](https://gerrit-documentation.storage.googleapis.com/Documentation/2.12.3/intro-user.html#upload-change)

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

## Cross-repo changes

Changes in two or more separate repos will be automatically tracked for you by
Gerrit if you use the same topic.

### Using jiri upload
Create branch with same name on all repos and upload the changes
```
# make and commit the first change
cd fuchsia/bin/fortune
git checkout -b new add_feature_foo
vim foo_related_files ...
git commit ...

# make and commit the second change in another repository
cd fuchsia/build
git checkout -b new add_feature_foo
vim more_foo_related_files ...
git commit ...

# Upload changes
jiri upload -multipart # default topic would be ${USER}-add_feature_foo
# or
jiri upload -multipart -topic="custom_topic"

# after the changes are reviewed, approved and submitted, cleanup the local branch
cd fuchsia/bin/fortune
git branch -d add_feature_foo

cd fuchsia/build
git branch -d add_feature_foo
```

### Using gerrit commands

```
# make and commit the first change, upload it with topic 'add_feature_foo'
cd fuchsia/bin/fortune
git checkout -b new add_feature_foo
vim foo_related_files ...
git commit ...
git push origin HEAD:refs/for/master%topic=add_feature_foo

# make and commit the second change in another repository
cd fuchsia/build
git checkout -b new add_feature_foo
vim more_foo_related_files ...
git commit ...
git push origin HEAD:refs/for/master%topic=add_feature_foo

# after the changes are reviewed, approved and submitted, cleanup the local branch
cd fuchsia/bin/fortune
git branch -d add_feature_foo

cd fuchsia/build
git branch -d add_feature_foo
```

Multipart changes are tracked in gerrit via topics, will be tested together,
and can be landed in Gerrit at the same time with `Submit Whole Topic`. Topics
can be edited via the web UI.

