#include <unistd.h>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <sys/mman.h>

#define MAX_SIZE 100000000
#define MIN_BLOCK_SIZE 128
#define MAX_ORDER 10
#define MIN_ORDER 10
#define BASE_NUM_BLOCKS 32
#define HIGHEST_BLOCK_SIZE  128 * 1024
const size_t HUGE_BLOCK = 4 * 1024 * 1024;
const size_t HUGE_ELEM = 2 * 1024 * 1024;

static bool heap_init = false;
static void *heap_base = nullptr;


struct MallocMetadata {
    MallocMetadata *next;
    MallocMetadata *prev;
    size_t size;
    size_t order;
    bool is_free;
    bool is_huge = false;
    bool is_mmap;
};
// static MallocMetadata *mmap_head = nullptr;

//--------Counters------
//overall (free and used) number of allocated blocks in the heap.
static size_t allocated_blocks = 0;

//overall number (free and used) of allocated bytes in the heap, excluding the bytes used by the meta-data structs.
static size_t allocated_bytes = 0;

//number of allocated blocks in the heap that are currently free.
static size_t free_blocks = 0;

//number of bytes in all allocated blocks in the heap that are currently free, excluding the bytes used by the meta-data structs.
static size_t free_bytes = 0;

//overall number of meta-data bytes currently in the heap.
static size_t meta_bytes = 0;

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




struct listOfBlocks {
    MallocMetadata *head = nullptr;

    MallocMetadata *popBlockMetaData() {
        if (!this->head) return nullptr;
        MallocMetadata *temp = this->head;
        this->head = this->head->next;
        temp->next = nullptr;
        temp->prev = nullptr;
        free_blocks--;
        return temp;
    }

    MallocMetadata *popSpecificBlockMetaData(MallocMetadata *block, bool is_mmap = false) {
        MallocMetadata *current = this->head;
        while (current && current != block) {
            current = current->next;
        }
        if (!current) return nullptr;

        if (current->prev) {
            current->prev->next = current->next;
        } else {
            this->head = current->next;
        }
        if (current->next) {
            current->next->prev = current->prev;
        }


        if (!is_mmap) {
            free_blocks--;
        }
        current->next = nullptr;
        current->prev = nullptr;
        return current;
    }

    void insertBlockMetaData(MallocMetadata *blockMetaData, bool is_mmap = false) {
        if (blockMetaData == nullptr) return;

        if (!is_mmap) {
            free_blocks++;
        }

        MallocMetadata *current = this->head;
        if (!this->head) {
            //empty list
            this->head = blockMetaData;
            blockMetaData->next = nullptr;
            blockMetaData->prev = nullptr;
            return;
        }
        if (blockMetaData < current) {
            //insert before head
            this->head = blockMetaData;
            blockMetaData->next = current;
            blockMetaData->prev = nullptr;
            current->prev = blockMetaData;
            return;
        }
        //walk to find insertion point
        while (current->next) {
            if (blockMetaData < current->next) {
                blockMetaData->prev = current;
                blockMetaData->next = current->next;
                current->next->prev = blockMetaData;
                current->next = blockMetaData;
                return;
            }
            current = current->next;
        }
        // Append at end
        current->next = blockMetaData;
        blockMetaData->prev = current;
        blockMetaData->next = nullptr;
    }

    size_t countBlocks() const {
        size_t count = 0;
        MallocMetadata *current = this->head;
        while (current) {
            count++;
            current = current->next;
        }
        return count;
    }
};


//--------helpers--------
static size_t order_to_size(int o) {
    return MIN_BLOCK_SIZE << o;
}

static int size_to_order(size_t n) {
    size_t total = n + sizeof(MallocMetadata);
    int order = 0;
    size_t block_size = MIN_BLOCK_SIZE;
    while (order < MAX_ORDER && block_size < total) {
        block_size <<= 1;
        order++;
    }
    return order;
}

MallocMetadata *findBuddyBlockAdr(MallocMetadata *block) {
    const size_t sizeOfBlock = order_to_size(block->order);
    const uintptr_t addr = reinterpret_cast<uintptr_t>(block);
    const uintptr_t buddyAddr = addr ^ sizeOfBlock;
    return reinterpret_cast<MallocMetadata *>(buddyAddr);
}


//------------------------Static array of lists with functions on it------------------------
static listOfBlocks free_lists[MAX_ORDER + 1] = {};
static listOfBlocks mmap_list;

size_t highestShadowMergePossibleOrder(MallocMetadata *block) {
    size_t workingOrder = block->order;
    MallocMetadata *workingOnShadowBlock = block;
    bool flag = true;

    while (flag) {
        const size_t sizeOfBlock = order_to_size(workingOrder);
        const uintptr_t addr = reinterpret_cast<uintptr_t>(workingOnShadowBlock);
        const uintptr_t buddyAddr = addr ^ sizeOfBlock;
        MallocMetadata *buddyBlock = reinterpret_cast<MallocMetadata *>(buddyAddr);

        if (buddyBlock->is_free && buddyBlock->order == workingOrder && workingOrder < MAX_ORDER) {
            workingOnShadowBlock = workingOnShadowBlock < buddyBlock ? workingOnShadowBlock : buddyBlock;
            workingOrder++;
            continue;
        }
        flag = false;
    }

    return workingOrder;
}

MallocMetadata *merge_two_blocks(MallocMetadata *block1, MallocMetadata *block2) {
    //call this function only if orders are the same and both are free...
    free_lists[block1->order].popSpecificBlockMetaData(block1);
    free_lists[block2->order].popSpecificBlockMetaData(block2);
    block1->is_free = true;
    block2->is_free = true;
    block1->order++;
    block2->order++;
    allocated_blocks--;
    allocated_bytes += sizeof(MallocMetadata);
    meta_bytes -= sizeof(MallocMetadata);
    free_bytes += sizeof(MallocMetadata);
    return block1 < block2 ? block1 : block2;
}

MallocMetadata *specialFooFooFunction(MallocMetadata *block, size_t orderToStop) {
    if (block->order < orderToStop) {
        MallocMetadata *buddyBlock = findBuddyBlockAdr(block);

        if (buddyBlock->is_free && buddyBlock->order == block->order && block->order < MAX_ORDER) {
            MallocMetadata *returnedBlock = merge_two_blocks(block, buddyBlock);
            return specialFooFooFunction(returnedBlock, orderToStop);  //here recursion takes place
        }
    }
    block->order = orderToStop;
    block->is_free = false;
    return block;
}

MallocMetadata *insert_block_aux(MallocMetadata *block) {
    MallocMetadata *buddyBlock = findBuddyBlockAdr(block);

    if (buddyBlock->is_free && buddyBlock->order == block->order && block->order < MAX_ORDER) {
        MallocMetadata *returnedBlock = merge_two_blocks(block, buddyBlock);
        return insert_block_aux(returnedBlock);  //here recursion takes place
    }

    return block;
}

void insert_block(MallocMetadata *block) {
    const size_t insertedSize = order_to_size(block->order);

    //we want to merge in a loop/recursion while buddy is also free... so it does it here.
    MallocMetadata *finalBlock = insert_block_aux(block);

    //when no more merging is possible
    block->is_free = true;
    free_bytes += insertedSize - sizeof(MallocMetadata);
    free_lists[block->order].insertBlockMetaData(finalBlock);
}

void halve_a_block(MallocMetadata *block) {
    block->order--;
    MallocMetadata *buddyBlock = findBuddyBlockAdr(block);
    buddyBlock->order = block->order;
    buddyBlock->is_free = true;
    buddyBlock->is_mmap = false;
    buddyBlock->next = nullptr;
    buddyBlock->prev = nullptr;
    free_lists[buddyBlock->order].insertBlockMetaData(buddyBlock);
    allocated_blocks++; //each halving adds an extra block
    allocated_bytes -= sizeof(MallocMetadata);
    free_bytes -= sizeof(MallocMetadata);
    meta_bytes += sizeof(MallocMetadata); //for each created metadata count it
}

MallocMetadata *pop_block(int order) {
    MallocMetadata *result = free_lists[order].popBlockMetaData();
    if (!result) {
        int workingOnOrder = order + 1;
        while (!result) {
            if (workingOnOrder > MAX_ORDER) return nullptr;
            result = free_lists[workingOnOrder].popBlockMetaData();
            if (result) break;
            workingOnOrder++;
        }
        //now we have a result we need to halve it
        while (order < workingOnOrder) {
            //when halving we are always looking already at the lowest address, no need to resave it.
            halve_a_block(result);
            workingOnOrder--;
        }
    }
    result->is_free = false;
    result->prev = nullptr;
    result->next = nullptr;
    free_bytes -= (order_to_size(order) - sizeof(MallocMetadata));
    return result;
}
//------------------------------------------------------------------------------------------


static void lazy_init() {
    if (heap_init) return;
    size_t initial_size = BASE_NUM_BLOCKS * HIGHEST_BLOCK_SIZE;
    //first sbrk
    void *curr_brk = sbrk(0);
    size_t mis = (intptr_t) curr_brk % initial_size;
    // second sbrk
    size_t pad = mis ? initial_size - mis : 0;
    void *raw = sbrk(pad + initial_size);
    if (raw == (void *) -1) return;               // error
    heap_base = (char *) raw + pad;
    for (size_t i = 0; i < BASE_NUM_BLOCKS; ++i) {
        void *addr = (void *) ((intptr_t) heap_base + i * HIGHEST_BLOCK_SIZE);
        MallocMetadata *new_meta = (MallocMetadata *) addr;
        new_meta->size = HIGHEST_BLOCK_SIZE - sizeof(MallocMetadata);
        new_meta->order = MAX_ORDER;
        new_meta->is_free = true;
        new_meta->is_mmap = false;
        new_meta->next = nullptr;
        new_meta->prev = nullptr;
        insert_block(new_meta);
    }
    // TODO: maybe update in insert??
    allocated_blocks += BASE_NUM_BLOCKS;
    meta_bytes = BASE_NUM_BLOCKS * sizeof(MallocMetadata);
    allocated_bytes = BASE_NUM_BLOCKS * (HIGHEST_BLOCK_SIZE - sizeof(MallocMetadata));
    heap_init = true;
}

static void *mmap_alloc(size_t size) {
    size_t totalSize = size + sizeof(MallocMetadata);
    //TODO: check prot&flags
    void *addr = mmap(nullptr, totalSize,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1, 0);
    if (addr == MAP_FAILED)
        return nullptr;

    MallocMetadata *new_meta = (MallocMetadata *) addr;
    new_meta->size = size;
    new_meta->order = 11;
    new_meta->is_free = false;
    new_meta->is_mmap = true;
    new_meta->next = nullptr;
    new_meta->prev = nullptr;
    mmap_list.insertBlockMetaData(new_meta, true);

    allocated_blocks++;
    allocated_bytes += size;
    meta_bytes += sizeof(MallocMetadata);

    return (void *) (new_meta + 1);
}

static void mmap_free(MallocMetadata *p_meta) {
    //delete from list
    if (p_meta == nullptr) return;
    MallocMetadata* returned_block = mmap_list.popSpecificBlockMetaData(p_meta, true);
    if (returned_block == nullptr) return;
    size_t prevSize = returned_block->size;
    size_t totalSize = returned_block->size + sizeof(MallocMetadata);
    //TODO: if munmap failed?
    munmap(returned_block, totalSize);
    allocated_blocks--;
    allocated_bytes -= prevSize;
    meta_bytes -= sizeof(MallocMetadata);
}
void *huge_alloc(size_t size) {
    size_t total = size + sizeof(MallocMetadata);
    void *addr = mmap_alloc(size);
    if (!addr) return nullptr;

    MallocMetadata *p_meta = ((MallocMetadata *) addr) - 1;
    p_meta->is_huge = true;
    madvise(p_meta, total, MADV_HUGEPAGE);
    return (void *) (p_meta + 1);
}

//---------------------------------------------------------------------------------------------------



void *smalloc(size_t size) {
    size_t totalSize = size + sizeof(MallocMetadata);
    // init once
    lazy_init();
    if (!heap_base) return nullptr;

    if (size == 0) {
        return nullptr;
    }

    //challenge 3
    if (totalSize >= HIGHEST_BLOCK_SIZE) {
        if (size >= HUGE_BLOCK) {
            return huge_alloc(size);
        }
        return mmap_alloc(size);
    }



    int order = size_to_order(size);
    // search for free block in free_lists

    MallocMetadata *block = pop_block(order);
    if (!block) return nullptr;

    return (void *) (block + 1);

}


void *scalloc(size_t num, size_t size) {
    void *block_ptr = nullptr;
    if (num != 0 && size > HUGE_ELEM)
        block_ptr = huge_alloc(num * size);
    else {
        block_ptr = smalloc(size * num);
    }
    if (block_ptr == nullptr) return nullptr;
    std::memset(block_ptr, 0, size * num);
    return block_ptr;
}

void sfree(void *p) {
    if (p == nullptr) return;
    MallocMetadata *p_meta = ((MallocMetadata *) p) - 1;
    if (p_meta->is_free) return;
    if (p_meta->is_mmap) {
        mmap_free(p_meta);
        return;
    }

    insert_block(p_meta);
    return;
}


void *srealloc(void *oldp, size_t size) {
    size_t totalSize = size + sizeof(MallocMetadata);
    if (size == 0) {
        return nullptr;
    }
    if (oldp == nullptr) return smalloc(size);

    MallocMetadata *oldp_meta = ((MallocMetadata *) oldp) - 1;
    if (oldp_meta->is_free) return nullptr;


    //mmap case
    if (oldp_meta->is_mmap) {
        if (oldp_meta->size == size) {
            return oldp;
        }
        void *new_block = smalloc(size);
        if (new_block == nullptr) return nullptr;
        std::memmove(new_block, oldp, size);
        sfree(oldp);
        return new_block;
    }


    // not mmap
    if (totalSize > MAX_SIZE) return nullptr;
    if (totalSize <= order_to_size(oldp_meta->order)) {
        //reuse this space
        //TODO: ask if to free halves of the blocks if possible
        oldp_meta->is_free = false;
        return oldp;
    }

    // if size > old_size: try to merge with buddies
    //TODO: check if able to do this, if answer is yes so merge and use current block, otherwise allocate new block
    size_t totalOrder = 0;
    while (order_to_size(totalOrder) < totalSize) totalOrder++;

    //if we can merge blocks together to create the space
    if (highestShadowMergePossibleOrder(oldp_meta) >= totalOrder) {
        free_bytes -= order_to_size(totalOrder)-order_to_size(oldp_meta->order);
        return (void *)(specialFooFooFunction(oldp_meta, totalOrder) + 1);
    }

    // if we cant merge (not enough free buddies): new allocation
    void *new_block = smalloc(size);
    if (new_block == nullptr) return nullptr;
    std::memmove(new_block, oldp, size);
    if (!oldp_meta->is_free) sfree(oldp);
    return new_block;

}


// size_t _free_blocks_in_order(size_t order) {
//     if (order == 11) return mmap_list.countBlocks();
//     return free_lists[order].countBlocks();
// }