#include <unistd.h>

const int MAX_SIZE = 100000000;

void *smalloc(size_t size) {
    if (size == 0 || size > MAX_SIZE) {
        return nullptr;
    }
    void *newProgPtr = sbrk(size);
    if (newProgPtr == (void *) -1) {
        return nullptr;
    }
    return newProgPtr;
}