#include "allocator.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define ALIGNMENT 16UL
#define CHUNK_SIZE (1UL << 20)

typedef struct block block_t;

typedef struct chunk chunk_t;

struct block {
    size_t size;
    int free;
    block_t *prev_phys;
    block_t *next_phys;
    block_t *prev_free;
    block_t *next_free;
};

struct chunk {
    void *addr;
    size_t size;
    chunk_t *next;
};

static block_t *free_list = NULL;
static chunk_t *chunks = NULL;

static size_t align_up(size_t value) {
    return (value + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

static size_t block_payload_offset(void) {
    return align_up(sizeof(block_t));
}

static void *block_to_payload(block_t *block) {
    return (void *)((char *)block + block_payload_offset());
}

static block_t *payload_to_block(void *ptr) {
    return (block_t *)((char *)ptr - block_payload_offset());
}

static void free_list_remove(block_t *block) {
    if (block->prev_free) {
        block->prev_free->next_free = block->next_free;
    } else if (free_list == block) {
        free_list = block->next_free;
    }
    if (block->next_free) {
        block->next_free->prev_free = block->prev_free;
    }
    block->prev_free = NULL;
    block->next_free = NULL;
}

static void free_list_insert(block_t *block) {
    block->free = 1;
    block->prev_free = NULL;
    block->next_free = free_list;
    if (free_list) {
        free_list->prev_free = block;
    }
    free_list = block;
}

static chunk_t *register_chunk(void *addr, size_t size) {
    chunk_t *chunk = (chunk_t *)mmap(NULL, align_up(sizeof(chunk_t)), PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (chunk == MAP_FAILED) {
        return NULL;
    }
    chunk->addr = addr;
    chunk->size = size;
    chunk->next = chunks;
    chunks = chunk;
    return chunk;
}

static block_t *extend_heap(size_t minimum_size) {
    size_t request_size = minimum_size + block_payload_offset();
    size_t chunk_size = CHUNK_SIZE;
    while (chunk_size < request_size) {
        chunk_size <<= 1;
    }

    void *memory = mmap(NULL, chunk_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        return NULL;
    }
    if (!register_chunk(memory, chunk_size)) {
        munmap(memory, chunk_size);
        return NULL;
    }

    block_t *block = (block_t *)memory;
    block->size = chunk_size - block_payload_offset();
    block->free = 1;
    block->prev_phys = NULL;
    block->next_phys = NULL;
    block->prev_free = NULL;
    block->next_free = NULL;
    free_list_insert(block);
    return block;
}

static block_t *split_block(block_t *block, size_t requested_size) {
    size_t remaining = block->size - requested_size;
    if (remaining <= block_payload_offset() + ALIGNMENT) {
        return block;
    }

    char *split_at = (char *)block_to_payload(block) + requested_size;
    block_t *rest = (block_t *)split_at;
    rest->size = remaining - block_payload_offset();
    rest->free = 1;
    rest->prev_phys = block;
    rest->next_phys = block->next_phys;
    rest->prev_free = NULL;
    rest->next_free = NULL;
    if (rest->next_phys) {
        rest->next_phys->prev_phys = rest;
    }
    block->next_phys = rest;
    block->size = requested_size;
    free_list_insert(rest);
    return block;
}

static block_t *coalesce(block_t *block) {
    while (block->next_phys && block->next_phys->free) {
        block_t *next = block->next_phys;
        free_list_remove(next);
        block->size += block_payload_offset() + next->size;
        block->next_phys = next->next_phys;
        if (block->next_phys) {
            block->next_phys->prev_phys = block;
        }
    }

    while (block->prev_phys && block->prev_phys->free) {
        block_t *prev = block->prev_phys;
        free_list_remove(prev);
        prev->size += block_payload_offset() + block->size;
        prev->next_phys = block->next_phys;
        if (prev->next_phys) {
            prev->next_phys->prev_phys = prev;
        }
        block = prev;
    }

    return block;
}

int mm_init(void) {
    return 0;
}

void *mm_malloc(size_t size) {
    if (size == 0) {
        size = 1;
    }
    size = align_up(size);

    for (block_t *block = free_list; block; block = block->next_free) {
        if (block->free && block->size >= size) {
            free_list_remove(block);
            block->free = 0;
            split_block(block, size);
            return block_to_payload(block);
        }
    }

    block_t *block = extend_heap(size);
    if (!block) {
        return NULL;
    }
    free_list_remove(block);
    block->free = 0;
    split_block(block, size);
    return block_to_payload(block);
}

void mm_free(void *ptr) {
    if (!ptr) {
        return;
    }
    block_t *block = payload_to_block(ptr);
    block->free = 1;
    block = coalesce(block);
    free_list_insert(block);
}

void *mm_realloc(void *ptr, size_t size) {
    if (!ptr) {
        return mm_malloc(size);
    }
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    size = align_up(size);
    block_t *block = payload_to_block(ptr);
    if (block->size >= size) {
        split_block(block, size);
        return ptr;
    }

    void *new_ptr = mm_malloc(size);
    if (!new_ptr) {
        return NULL;
    }
    memcpy(new_ptr, ptr, block->size);
    mm_free(ptr);
    return new_ptr;
}
