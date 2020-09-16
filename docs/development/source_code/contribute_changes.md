# Contribute changes

This guide provides instructions on how to submit your contribution to the
Fuchsia project.

## Prerequisites

Fuchsia manages commits through
[Gerrit](https://fuchsia-review.googlesource.com){:.external}.

Before you begin, you need to:

*   [Download the Fuchsia source code](/docs/get-started/get_fuchsia_source.md).

    Note: You can complete the next prerequisite items while downloading the
    Fuchsia source code.

*   [Sign the Google Contributor License Agreements (CLA)](#sign-the-google-cla).

*   [Generate a cookie to authenticate you in Gerrit](#generate-a-cookie).

### Sign the Google CLA {#sign-the-google-cla}

Do the following:

1.  Go to the Google Developers'
    [Contributor License Agreements](https://cla.developers.google.com/){:.external}
    page.
1.  Sign the agreement on behalf of **Only Yourself** or **Your Employer**.

### Generate a cookie {#generate-a-cookie}

Do the following:

1.  Log into [Gerrit](https://fuchsia-review.googlesource.com){:.external}.
1.  Go to
    [https://fuchsia.googlesource.com](https://fuchsia.googlesource.com){:.external}.
1.  At the top of the page, click **Generate Password**.
1.  Copy the generated code and run it in a terminal of your workstation.

## Create a change in Gerrit {#create-a-change-in-gerrit}

To create a
[change](https://gerrit-review.googlesource.com/Documentation/concept-changes.html){:.external}
in Gerrit, do the following:

1.  Go to your Fuchsia directory, for example:

    ```posix-terminal
    cd ~/fuchsia
    ```

1.  Create a new branch:

    ```posix-terminal
    git checkout -b <branch_name>

    ```

1.  Create or edit files in the new branch.

1.  Add the updated files:

    ```posix-terminal
    git add <files>
    ```

1.  Commit the updated files and
    [write a change message](#write-a-change-message):

    ```posix-terminal
    git commit
    ```

1.  Upload the commit to Gerrit:

    ```posix-terminal
    jiri upload
    ```

    If you want to use the `git` command instead, run the following command:

    ```posix-terminal
    git push origin HEAD:refs/for/master
    ```

See the
[Gerrit documentation](https://gerrit-documentation.storage.googleapis.com/Documentation/2.12.3/intro-user.html#upload-change){:.external}
for more information.

### Create and upload a patch

After creating a change, to upload a patch to your change, do the following:

1.  Create or edit files in the same branch.
1.  Add the updated files:

    ```posix-terminal
    git add <files>
    ```

1.  Include the patch in the same commit using the `--amend` option:

    ```posix-terminal
    git commit --amend
    ```

1.  Upload the patch to Gerrit:

    ```posix-terminal
    jiri upload
    ```

### Resolve merge conflicts {#resolve-merge-conflicts}

When Gerrit warns you of merge conflicts in your change, do the following:

1.  Rebase from `origin/master`, which reveals the files that cause merge
    conflicts:

    ```posix-terminal
    git rebase origin/master
    ```

1.  Edit those files to resolve the conflicts and finish the rebase:

    ```posix-terminal
    git add <files_with_resolved_conflicts>
    ```

    ```posix-terminal
    git rebase --continue
    ```

1.  Upload the patch to your change:

    ```posix-terminal
    git commit --amend
    ```

    ```posix-terminal
    jiri upload
    ```

### Delete your local branch

After the change is submitted, you may delete your local branch:

```posix-terminal
git branch -d <branch_name>
```

## Write a change message {#write-a-change-message}

When writing a change message, follow these guidelines:

*   [Add commit message tags](#add-commit-message-tags)
*   [Add test instructions](#add-test-instructions)

### Add commit message tags {#add-commit-message-tags}

Include `[tags]` in the subject of a commit message to indicate which module,
library, and app are affected by your change. For instance, use `[docs]` for
documentation, `[zircon]` for zircon, and `[fidl]` for FIDL.

The following example of a commit message shows the tags in the subject:

<pre>
<b>[parent][component]</b> Update component in Topaz.

Write the details of a commit message here.

Test: Added test X.
</pre>

You can view the commit history of the files you've edited to check for the tags
used previously. See these examples:

*   [https://fuchsia-review.googlesource.com/c/peridot/+/113955](https://fuchsia-review.googlesource.com/c/peridot/+/113955){:.external}
*   [https://fuchsia-review.googlesource.com/c/topaz/+/114013](https://fuchsia-review.googlesource.com/c/topaz/+/114013){:.external}

If the subject of a commit message doesn't include tags, Gerrit flags your
change with `Needs Label: Commit-Message-has-tags`.

### Add test instructions {#add-test-instructions}

If a change requires non-obvious manual testing for validation, describe those
testing steps in the change description beginning with `Test:`, for example:

```none
Test: Write the test instructions here.
```

If the instructions are complex, create a bug and provide a link to that bug in
the change description. If the change doesn't intend to change behavior,
indicate that fact in the commit message.

In some cases, certain behavior changes cannot be tested because Fuchsia lacks
some particular piece of infrastructure. If so, create an issue in the tracker
about the necessary infrastructure support and provide the bug number in the
change description, in addition to describing how the change is tested manually,
for example:

```none
Test: Manually tested that [...]. Automated testing needs US-XXXX.
```

Developers are responsible for high-quality automated testing of their code.
Reviewers are responsible for pushing back on changes that do not include
sufficient tests. See
[Fuchsia testability rubrics](/docs/concepts/testing/testability_rubric.md) for
more information on how to introduce testable and tested code in the Fuchsia
project.

## Manage changes that span multiple repositories

To understand how to manage changes that span different repositories (petals),
see the following pages:

*   [Working across different petals](/docs/development/source_code/working_across_petals.md)
*   [Upload changes from multiple repositories](/docs/development/source_code/upload_changes_from_multiple_repositories.md)

See [Source code layout](/docs/concepts/source_code/layout.md) for more
information on the structure of the Fuchsia repository.

