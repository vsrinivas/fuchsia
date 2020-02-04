# Documentation Standards

A document about what to document and how to document it for people who create things that need
documentation.

## Why document?

Fuchsia is a new operating system. Effective documentation allows new people to join and grow the
project by having all necessary documentation be clear and concise.

## Who is the audience?

The documentation described here is intended to address a technical audience, i.e. those who expect
to implement or exercise APIs or understand the internal dynamics of the operating system. These
standards are not intended for end-user product documentation.

## What should I document?

Document protocols, introduce essential concepts, explain how everything fits together.

- Conventions: e.g. this document about documentation, code style
- System Design: e.g. network stack, compositor, kernel, assumptions
- APIs: e.g. FIDL protocols, library functions, syscalls
- Protocols: e.g. schemas, encodings, wire formats, configuration files
- Tools: e.g. `bootserver`, `netcp`, `fx`
- Workflows: e.g. environment set up, test methodologies, where to find various
  parts, how to get work done

## Where should I put documents?  What goes where?

Documentation that is only for developers working on creating or maintaining
a specific part of the code should be kept in the same directory as the source code.

Documentation that should be generally available to developers must be
available in one of two locations:

* Zircon specific documentation should be created in `/docs/zircon`.
* Fuchsia documentation that is not specific to Zircon specific should
   be created in `/docs`.  In the `/docs/` directory, you should create your
   documentation or images in one of these sub-directories:
    * `best-practices`
       General best practice guidelines on how to develop with Fuchsia source.
       If you create best practice documentation about about using a specific
       feature of Fuchsia, you should create the documentation in the same
       directory as the other documentation for that specific feature.
    *  `development`
        Instructions, tutorials, and procedural documentation for developers
        that are working on Fuchsia. This directory includes documentation
        on how to get started, build, run, and test Fuchsia and software
        running on devices operating Fuchsia. You should organize the content
        that you create by specific activities, such as testing, getting
        started, or by workflow topic.
    * `the-book`
        Concept and developer guides about the features of Fuchsia. You
        should organize the content that you create by specific features.
    * `images`
        Images that are used in the documentation. You should place images in
        this common directory and avoid placing images in the same directory
        as documentation.

## What documentation should I create?

Most documentation can be divided into four categories:

- [Reference](documentation_types.md#reference-documentation) - Information-oriented documentation
- [Conceptual](documentation_types.md#conceptual-documentation) - Understanding-oriented
  documentation
- [Procedural](documentation_types.md#procedural-documentation)
    - How to - Goal-oriented documentation
    - Codelab - Learning-oriented documentation

See [Documentation Types](documentation_types.md) for more information.

However, comments in your code are very important for maintainability and helping other people
understand your code. See the [Code Comment Guidelines](documentation_comments.md) for style guidelines
related to comments for your code.

## What documentation style guidelines should I follow?

It is important to try to follow documentation style guidelines to ensure that the documentation
created by a large number of contributors can flow together. See
[Documentation Style Guide](documentation_style_guide.md).

## How can I link to source code in my documentation?

Use absolute paths starting with '/', like [`/zircon/public/sysroot/BUILD.gn`](/zircon/public/sysroot/BUILD.gn).
Never use relative paths with ".." that point to content outside of `/docs`.

## How can I expose my documentation?

Documentation is only useful when users can find it. Adding links to or from existing documentation
greatly improves the chances that someone can find your documentation.

Tips for leaving breadcrumbs:

- Table of contents: Add links to documentation in the left sided navigation
  on fuchsia.dev. See
  [Change table of contents navigation](documentation_navigation_toc.md).
- Top-down linkage: Add links from more general documents to more specific documents to help
  readers learn more about specific topics. The [Fuchsia book](/docs/concepts/README.md) is a good
  starting point for top-down linkage.
- Bottom-up linkage: Add links from more specific documents to more general documents to help
  readers understand the full context context of the topics being discussed. Adding links from
  module, class, or protocol documentation comments to conceptual documentation overviews can be
  particularly effective.
- Sideways linkage: Add links to documents on subjects that help readers better understand the
  content of your document.

