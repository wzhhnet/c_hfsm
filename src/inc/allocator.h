/*
 * A very simple utilities that exchange event asynchronously via another thread.
 *
 * Author wanch
 * Date 2022/11/23
 * Email wzhhnet@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef UTILS_ALLOCATOR_H
#define UTILS_ALLOCATOR_H

#include <stdlib.h>
#include <pthread.h>
#include "list.h"
#include "errors.h"
#include "bitops.h"

#ifdef __cplusplus
    extern "C" {
#endif

#define ALLOCATOR_DECLARE(PRODUCT, TYPE)    \
    struct PRODUCT##_element {              \
        int bit;                            \
        TYPE element;                       \
    };                                      \
    struct PRODUCT##_allocator {            \
        pthread_mutex_t mutex;              \
        unsigned char size;                 \
        unsigned int *bits;                 \
        struct PRODUCT##_element *array;    \
    }

#define ALLOCATOR_DEFINE(PRODUCT, VAR)         \
    struct PRODUCT##_allocator  VAR

#define ALLOCATOR_CREATE(PRODUCT, allocator, size)  \
    PRODUCT##_allocator_create(allocator, size)
#define ALLOCATOR_DESTORY(PRODUCT, allocator)       \
    PRODUCT##_allocator_destory(allocator)
#define ALLOCATOR_ALLOC(PRODUCT, allocator)         \
    PRODUCT##_allocator_alloc(allocator)
#define ALLOCATOR_FREE(PRODUCT, allocator, element) \
    PRODUCT##_allocator_free(allocator, element)

#define ALLOCATOR_IMPLEMENT(PRODUCT, TYPE)                                      \
    static int PRODUCT##_allocator_create(                                      \
        struct PRODUCT##_allocator *inst, unsigned char size)                   \
    {                                                                           \
        char *mem;                                                              \
        unsigned int b_size, e_size;                                            \
        RETURN_IF_TRUE(!size, UTILS_ERR_POOL_SIZE);                             \
        memset(inst, 0, sizeof(struct PRODUCT##_allocator));                    \
        b_size = sizeof(unsigned int) * BITS_TO_WORDS(size);                    \
        e_size = sizeof(struct PRODUCT##_element) * size;                       \
        mem = (char*)malloc(b_size + e_size);                                   \
        RETURN_IF_NULL(mem, UTILS_ERR_POOL_MEM);                                \
        pthread_mutex_init(&inst->mutex, NULL);                                 \
        inst->bits = (unsigned int*)mem;                                        \
        inst->size = size;                                                      \
        inst->array = (struct PRODUCT##_element*)(mem + b_size);                \
        bitmask_init(inst->bits, (int)inst->size);                              \
        return UTILS_SUCC;                                                      \
    };                                                                          \
    static void PRODUCT##_allocator_destory(struct PRODUCT##_allocator *inst)   \
    {                                                                           \
        if (inst->bits) {                                                       \
            free(inst->bits);                                                   \
            inst->bits = NULL;                                                  \
            inst->array = NULL;                                                 \
        }                                                                       \
        inst->size = 0;                                                         \
        pthread_mutex_destroy(&inst->mutex);                                    \
    };                                                                          \
    static TYPE* PRODUCT##_allocator_alloc(struct PRODUCT##_allocator *inst)    \
    {                                                                           \
        int max, bit;                                                           \
        TYPE* s = NULL;                                                         \
        pthread_mutex_lock(&inst->mutex);                                       \
        max = (int)inst->size;                                                  \
        bit = bitmask_ffz(inst->bits, max);                                     \
        if (bit >= 0 && bit < max) {                                            \
            inst->array[bit].bit = bit;                                         \
            bitmask_set(inst->bits, bit);                                       \
            s = &inst->array[bit].element;                                      \
        }                                                                       \
        pthread_mutex_unlock(&inst->mutex);                                     \
        return s;                                                               \
    };                                                                          \
    static int PRODUCT##_allocator_free(                                        \
            struct PRODUCT##_allocator *inst, TYPE *element)                    \
    {                                                                           \
        struct PRODUCT##_element *e;                                            \
        e = container_of(element, struct PRODUCT##_element, element);           \
        if (e->bit >= 0 && e->bit < (int)inst->size) {                          \
            pthread_mutex_lock(&inst->mutex);                                   \
            bitmask_clear(inst->bits, e->bit);                                  \
            pthread_mutex_unlock(&inst->mutex);                                 \
            return UTILS_SUCC;                                                  \
        }                                                                       \
        return UTILS_ERR_POOL_FREE;                                             \
    }

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /*!< UTILS_ALLOCATOR_H */

