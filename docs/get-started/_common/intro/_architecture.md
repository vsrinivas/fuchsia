![Data table showing high-level diagram of the entire Fuchsia system
architecture, highlighting core components and subsystems.]
(/docs/get-started/images/intro/fuchsia-architecture.png){: width="1080"}

The following architectural principles guide Fuchsia's design and development:

* **Simple:**
  Fuchsia makes it easy to create, maintain, and integrate software and hardware across a wide range of devices.

* **Secure:**
  Fuchsia has a kernel and software model designed for modern computing.

* **Updatable:**
  As a modular operating system, Fuchsia allows the kernel, drivers, and software components to be independently updatable.

* **Performant:**
  Fuchsia is designed for real world product requirements and optimized for performance.

The core of the system is [Zircon][glossary.zircon], a kernel and collection of
libraries for handling system startup and bootstrapping.
All other system components are implemented in user space and isolated,
reinforcing the **principle of least privilege**. This includes:

*   Device drivers
*   Filesystems
*   Network stacks

[glossary.zircon]: /docs/glossary/README.md#zircon