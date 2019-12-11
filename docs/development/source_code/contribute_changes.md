# Contribute changes

Fuchsia manages commits through Gerrit at
[https://fuchsia-review.googlesource.com](https://fuchsia-review.googlesource.com).

To submit your contribution to the Fuchsia project, follow the instructions in the sections below.

## Prerequisites

Before you submit your first contribution to the Fuchsia project, you need to
[sign the Google Contributor License Agreements (CLA)](#sign-the-google-cla) and [generate a cookie to authenticate you in Gerrit](#generate-a-cookie).

### Sign the Google CLA {#sign-the-google-cla}

To sign the Google CLA, do the following:

1.  Go to the Google Developers' [Contributor License Agreements](https://cla.developers.google.com/) page.
1.  Sign the agreement on behalf of **Only Yourself** or **Your Employer**.

### Generate a cookie {#generate-a-cookie}

To generate the cookie, do the following:

1.  Log into [Gerrit](https://fuchsia-review.googlesource.com).
1.  At the top of [https://fuchsia.googlesource.com](https://fuchsia.googlesource.com), click **Generate Password**.
1.  Copy the generated code and run it in a terminal of your workstation.

## Create a Change in Gerrit {#create-a-change-in-gerrit}

Follow these steps to create a Change in Gerrit:

1.  Create a new branch:

    ```
    git checkout -b <branch_name>

    ```
1.  Create or edit files in the new branch.
1.  Add the changes:

    ```
    git add <files>
    ```
1.  Commit the changes (see [Add commit message tags](#add-commit-message-tags)):

    ```
    git commit
    ```

1.  Upload the changes to Gerrit:

    ```
    jiri upload
    ```

    *   If you want to use the `git` command instead, run the
        following command:

        ```
        git push origin HEAD:refs/for/master
        ```

Note: If you want to upload the changes with a custom topic,
see [Upload changes from multiple repositories](/docs/development/source_code/upload_changes_from_multiple_repositories.md) for details.

At any time, if you want to make changes to your patch, use `--amend`:

```
git commit --amend
```

Once the change is submitted, clean up the branch:

```
git branch -d <branch_name>
```

See the [Gerrit documentation](https://gerrit-documentation.storage.googleapis.com/Documentation/2.12.3/intro-user.html#upload-change) for more information.

## Add commit message tags {#add-commit-message-tags}

You must include `[tags]` in the subject of a commit message to indicate which
module, library, and app are affected by your Change.

See the following example of a commit message, which shows the tags in the
subject:

```
[parent][component] Update component in Topaz.

<The details of the commit message here.>

Test: Added test X
```

For the tags, use `[docs]` for documentation, `[zircon]` for zircon, `[fidl]` for FIDL, and more. You can view the commit history of the files you've edited to check for the tags used previously.

See these examples:

*   [https://fuchsia-review.googlesource.com/c/zircon/+/112976](https://fuchsia-review.googlesource.com/c/zircon/+/112976)
*   [https://fuchsia-review.googlesource.com/c/garnet/+/110795](https://fuchsia-review.googlesource.com/c/garnet/+/110795)
*   [https://fuchsia-review.googlesource.com/c/peridot/+/113955](https://fuchsia-review.googlesource.com/c/peridot/+/113955)
*   [https://fuchsia-review.googlesource.com/c/topaz/+/114013](https://fuchsia-review.googlesource.com/c/topaz/+/114013)

Note: Gerrit flags your Change with
`Needs Label: Commit-Message-has-tags` if the subject of the commit message doesn't include tags.

## Add test instructions {#add-test-instructions}

If a change requires non-obvious manual testing for validation, those testing
steps should be described in a line in the change description beginning with
`Test:`.

If the instructions are more elaborate, they can be added to a linked
bug. If the change does not intend to change behavior, the commit message should
indicate as such.

In some cases, we are not able to test certain behavior changes because we lack
some particular piece of infrastructure. In that case, we should have an issue
in the tracker about creating that infrastructure and the test label should
mention the bug number in addition to describing how the change was manually
tested:

```
Test: Manually tested that [...]. Automated testing needs US-XXXX
```

Developers are responsible for high-quality automated testing of their code.
Reviewers are responsible for pushing back on changes that do not include
sufficient tests.

Note: See [Fuchsia testability rubrics](/docs/concepts/testing/testability_rubric.md)
for more information on how to introduce testable and tested code in the Fuchsia project.

## Resolve merge conflicts {#resolve-merge-conflicts}

1.  Rebase from `origin/master`, which reveals the files that cause merge conflicts:

    ```
    git rebase origin/master
    ```

1.  Edit those files to rsesolve the conflicts and finish the rebase:

    ```
    git add <files_with_resolved_conflicts>
    git rebase --continue
    ```

1.  Commit and upload your changes:

    ```
    git commit --amend
    jiri upload
    ```

## Manage changes that span multiple repositories

To understand how to manage changes that span different repositories (petals),
see the following pages:

*   [Working across different petals](/docs/development/workflows/working_across_petals.md)
*   [Upload changes from multiple repositories](/docs/development/source_code/upload_changes_from_multiple_repositories.md)

More information on the structure of the `fuchsia.git` respository is available in
[Source code layout](/docs/development/source_code/layout.md).
