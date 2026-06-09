#include <stdlib.h>
#define VMA_SYSTEM_ALIGNED_MALLOC(size, alignment) \
    ({ \
        void* ptr = NULL; \
        if (posix_memalign(&ptr, alignment, size) != 0) ptr = NULL; \
        ptr; \
    })

#define VMA_SYSTEM_ALIGNED_FREE(ptr) free(ptr)
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"