# RadixSort/VK

RadixSort/VK is a high-performance sorting library for Vulkan 1.2.

Features include:

* Ultra-fast stable sorting of 32‑bit or 64‑bit keyvals
* Key size is declared at sort time
* Indirectly dispatchable
* Simple to integrate in a Vulkan 1.2 environment

## Usage

See [`radix_sort_vk.h`](platforms/vk/include/radix_sort/platforms/vk/radix_sort_vk.h).

### Device Support

The following architectures are supported:

Vendor | Architecture  | 32‑bit Keyvals     | 64‑bit Keyvals  | Notes
-------|---------------|:------------------:|:---------------:|------
NVIDIA | sm_35+        | ✔                  | ✔               |
AMD    | GCN           | ✔                  | ✔               |
ARM    | Bifrost4      | ✔                  | ✔               |
ARM    | Bifrost8      | ✔                  | ✔               |
Intel  | GEN8+         | ✔                  | ✔               |

## Benchmarks

### NVIDIA GeForce RTX 2060
![NVIDIA GeForce RTX 2060](docs/images/nvidia_rtx2060.png)

### NVIDIA Quadro K2200
![NVIDIA Quadro K2200](docs/images/nvidia_k2200.png)

### AMD Radeon RX 560
![AMD Radeon RX 560](docs/images/amd_rx560.png)

### Intel HD Graphics 615
![Intel HD Graphics 615](docs/images/intel_hd615.png)

### ARM Mali G52
![ARM Mali G52](docs/images/arm_g52.png)

### ARM Mali G31
![ARM Mali G31](docs/images/arm_g31.png)

## References
