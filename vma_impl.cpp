#include <stdlib.h>

#if defined(_WIN32) || defined(_WIN64)
    #include <malloc.h>
    #define VMA_SYSTEM_ALIGNED_MALLOC(size, alignment) _aligned_malloc(size, alignment)
    #define VMA_SYSTEM_ALIGNED_FREE(ptr)               _aligned_free(ptr)

#elif defined(__ANDROID__)
    #include <malloc.h>
    #define VMA_SYSTEM_ALIGNED_MALLOC(size, alignment) memalign(alignment, size)
    #define VMA_SYSTEM_ALIGNED_FREE(ptr)               free(ptr)

#elif defined(__linux__) || defined(__APPLE__)
    #define VMA_SYSTEM_ALIGNED_MALLOC(size, alignment) \
        ({ \
            void* ptr = NULL; \
            if (posix_memalign(&ptr, alignment, size) != 0) ptr = NULL; \
            ptr; \
        })
    #define VMA_SYSTEM_ALIGNED_FREE(ptr)               free(ptr)

#endif

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"