#include <unistd.h>
#include <cstddef>
#include <cstring>

const int MAX_SIZE = 100000000;

struct MallocMetadata {
    size_t size;
    bool is_free;
    MallocMetadata *next;
    MallocMetadata *prev;
};

static MallocMetadata *head = nullptr;
//--------Counters------
static size_t allocated_blocks = 0;
static size_t allocated_bytes = 0;
static size_t free_blocks = 0;
static size_t free_bytes = 0;
static size_t meta_bytes = 0;


void *smalloc(size_t size) {
    size_t totalSize = size + sizeof(MallocMetadata);
    if (size == 0 || size > MAX_SIZE) {
        return nullptr;
    }
    // search for free alocated block
    if (free_blocks > 0) {
        for (MallocMetadata *curr = head; curr; curr = curr->next) {
            if (curr->is_free && curr->size >= size) {
                curr->is_free = false;
                free_blocks--;
                free_bytes -= curr->size;
                return (void *) (curr + 1);
            }
        }
    }
    // not found so alocate new block and metadata
    void *brk = sbrk(0);
    if (sbrk(totalSize) == (void *) -1) {
        return nullptr;
    }

    allocated_blocks++;
    allocated_bytes += size;
    meta_bytes += sizeof(MallocMetadata);

    // build new metadata
    MallocMetadata *new_meta = (MallocMetadata *) brk;
    new_meta->size = size;
    new_meta->is_free = false;
    new_meta->next = nullptr;

    if (!head) {
        new_meta->prev = nullptr;
        head = new_meta;
    } else {
        MallocMetadata *last = head;
        while (last->next) last = last->next;
        last->next = new_meta;
        new_meta->prev = last;
    }

    //return ptr after metadata part
    return (void *) (new_meta + 1);
}


void *scalloc(size_t num, size_t size) {
    size_t totalSize = size * num + sizeof(MallocMetadata);
    if (size * num == 0 || size * num > MAX_SIZE) {
        return nullptr;
    }
    // search for free alocated block
    if (free_blocks > 0) {
        for (MallocMetadata *curr = head; curr; curr = curr->next) {
            if (curr->is_free && curr->size >= size * num) {
                curr->is_free = false;
                free_blocks--;
                free_bytes -= curr->size;
                std::memset(curr + 1, 0, size * num);
                return (void *) (curr + 1);
            }
        }
    }
    // not found so alocate new block and metadata
    void *brk = sbrk(0);
    if (sbrk(totalSize) == (void *) -1) {
        return nullptr;
    }

    allocated_blocks++;
    allocated_bytes += size * num;
    meta_bytes += sizeof(MallocMetadata);

    // build new metadata
    MallocMetadata *new_meta = (MallocMetadata *) brk;
    new_meta->size = size * num;
    new_meta->is_free = false;
    new_meta->next = nullptr;

    if (!head) {
        new_meta->prev = nullptr;
        head = new_meta;
    } else {
        MallocMetadata *last = head;
        while (last->next) last = last->next;
        last->next = new_meta;
        new_meta->prev = last;
    }

    void *block_ptr = (void *) (new_meta + 1);
    std::memset(block_ptr, 0, size * num);
    return block_ptr;
}

//void *scalloc(size_t num, size_t size) {
//    void* block_ptr = smalloc(size * num);
//    if (block_ptr == nullptr) return nullptr;
//    std::memset(block_ptr, 0, size * num);
//    return block_ptr;
//}

void sfree(void *p) {
    if (p == nullptr) return;
    MallocMetadata *p_meta = ((MallocMetadata *) p) - 1;
    if (p_meta->is_free) return;
    p_meta->is_free = true;
    free_blocks++;
    free_bytes += p_meta->size;
    return;
}

void *srealloc(void *oldp, size_t size) {
    if (size == 0 || size > MAX_SIZE) {
        return nullptr;
    }
    if (oldp == nullptr) return smalloc(size);


    MallocMetadata *oldp_meta = ((MallocMetadata *) oldp) - 1;
    if (size <= oldp_meta->size) {
        //reuse this space
        oldp_meta->is_free = false;
        return oldp;
    }

    void *new_block = smalloc(size);
    if (new_block == nullptr) return nullptr;
    std::memmove(new_block, oldp, size);
    if (!oldp_meta->is_free) sfree(oldp);
    return new_block;
}

// more methods
//Returns the number of allocated blocks in the heap that are currently free.
size_t _num_free_blocks() {
    return free_blocks;
}

//Returns the number of bytes in all allocated blocks in the heap that are currently free, excluding the bytes used by the meta-data structs.
size_t _num_free_bytes() {
    return free_bytes;
}

//Returns the overall (free and used) number of allocated blocks in the heap.
size_t _num_allocated_blocks() {
    return allocated_blocks;
}

//Returns the overall number (free and used) of allocated bytes in the heap, excluding the bytes used by the meta-data structs.
size_t _num_allocated_bytes() {
    return allocated_bytes;
}

//Returns the overall number of meta-data bytes currently in the heap.
size_t _num_meta_data_bytes() {
    return meta_bytes;
}

//Returns the number of bytes of a single meta-data structure in your system.
size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}

