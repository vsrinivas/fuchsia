#include <stdlib.h>

void* operator new(size_t s) {
    return ::malloc(s);
}

void* operator new[](size_t s) {
    return ::malloc(s);
}

void* operator new(size_t, void* p) {
    return p;
}

void* operator new[](size_t, void* p) {
    return p;
}

void operator delete(void* p) {
    ::free(p);
}

void operator delete[](void* p) {
    ::free(p);
}
