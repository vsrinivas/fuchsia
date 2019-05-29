#include <stdint.h>

struct DebugUartInfo {
    enum class Type {
        None,
        Port,
        Mmio,
    };

    uint64_t mem_addr;
    uint32_t io_port;
    uint32_t irq;
    Type type;
};

DebugUartInfo debug_uart_info();
