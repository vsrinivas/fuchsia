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

Keep your documentation in the source tree near the things it describes. The location of the
document depends on the type of document and its topic.

Preferred locations:

- Documents about a specific project should go into the `docs` folder at the root of that project's
  repository and be arranged by topic.
  e.g. `//apps/my-project/docs/my-feature.md`
- Documents about Fuchsia as a whole should go into the top-level `docs` repository itself.  e.g.
  `//docs/build_packages.md`

Alternate locations:

- Adding a `README.md` to the root of a project's repository may serve as a brief orientation to the
  project for first time visitors but this is not required.

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

## How can I expose my documentation?

Documentation is only useful when users can find it. Adding links to or from existing documentation
greatly improves the chances that someone can find your documentation.

Tips for leaving breadcrumbs:

- Top-down linkage: Add links from more general documents to more specific documents to help
  readers learn more about specific topics. The [Fuchsia book](../the-book/README.md) is a good
  starting point for top-down linkage.
- Bottom-up linkage: Add links from more specific documents to more general documents to help
  readers understand the full context context of the topics being discussed. Adding links from
  module, class, or protocol documentation comments to conceptual documentation overviews can be
  particularly effective.
- Sideways linkage: Add links to documents on subjects that help readers better understand the
  content of your document.
