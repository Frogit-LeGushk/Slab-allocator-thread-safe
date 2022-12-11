/***************************************
 * SLAB allocator                      *
 * Author: Acool4ik                    *
 *                                     *
 *      Params:                        *
 * Thread-safety                       *
 * Min blocks for allocate mem: 1Byte  *
 * Max blocks for allocate mem: 1GB    *
 *                                     *
 *      Complexity (API)               *
 * cache_setup: O(1)                   *
 * cache_release: O(K)                 *
 * cache_alloc: O(1*)                  *
 * cache_free: O(1)                    *
 * cache_shrink: O(K)                  *
 *                                     *
 * K - count of slabs                  *
 ***************************************/


#include <iostream>
#include <pthread.h>
#include <assert.h>

using namespace std;


/***********************
 * Structures and      *
 * global variables    *
 *                     *
 ***********************/

struct data_block {
    data_block * next = nullptr;
};
static const int DATA_BLOCK_SIZE = sizeof(data_block);

struct meta_block {
    meta_block * next = nullptr;
    data_block * head = nullptr;
    size_t cnt_objects= 0;
};
static const int META_BLOCK_SIZE = sizeof(meta_block);

static const size_t PAGE_SIZE = 4 * (1 << 10); // 4 KiB
static const int PAGE_SIZE_DEGREE_2 = 12;

struct cache {
    size_t  object_size;
    int     slab_order;
    size_t  cnt_objects;
    size_t  meta_block_offset;

    meta_block * free_list_slabs       = nullptr;
    meta_block * busy_list_slabs       = nullptr;
    meta_block * partbusy_list_slabs   = nullptr;
};

enum class SlabType {
    FREE = 1,
    BUSY,
    PARTBUSY
};

struct map_item {
    void * aligment_ptr  = nullptr;
    void * allocated_ptr = nullptr;
};

static const int max_map_items = (1 << 15);
static map_item map_item_array[max_map_items];

struct pthread_lock_quard {
    pthread_lock_quard(pthread_mutex_t& mtx) : mtx_(mtx) {
        pthread_mutex_lock(&mtx_); }
    ~pthread_lock_quard() {
        pthread_mutex_unlock(&mtx_); }
    void manual_unlock() {
        pthread_mutex_unlock(&mtx_); }
private:
    pthread_mutex_t& mtx_;
};

static pthread_mutex_t MTX = PTHREAD_MUTEX_INITIALIZER;

/**
 * It allocate memory for SLAB allocator.
 * It need for imitation BUDDY allocator,
 * which allocate big memory with
 * natural alignment
 *
 * \param order - degree of 2 (2^order), order [0,18]
 * in memory it is [4KiB, 1GiB]
 *
 * \return pointer to memory, that is aligment on
 * size = PAGE_SIZE * (1 << order)
 **/
static void * alloc_slab(int order) {
    assert(0 <= order && order <= 18);

    const int shift = PAGE_SIZE_DEGREE_2 + order;
    const size_t SLAB_SIZE = PAGE_SIZE * (1 << order);

    void * allocated_ptr = malloc(2*SLAB_SIZE);
    size_t aligment_numptr = (((size_t)allocated_ptr >> shift) << shift);
    void * aligment_ptr = (void *)(aligment_numptr + SLAB_SIZE);

    for (int i = 0; i < max_map_items; i++)
    if (map_item_array[i].allocated_ptr == nullptr) {

        map_item_array[i].allocated_ptr = allocated_ptr;
        map_item_array[i].aligment_ptr = aligment_ptr;
        break;
    }

    return aligment_ptr;
}
/**
 * It release memory, that has been allocated before
 * by alloc_slab. If no such pointer, then exit(1)
 **/
static void free_slab(void *slab) {
    for (int i = 0; i < max_map_items; i++)
    if (map_item_array[i].aligment_ptr == slab) {
        free(map_item_array[i].allocated_ptr);
        map_item_array[i] = {nullptr, nullptr};
        return;
    }
    exit(1);
}


/***********************
 * Debug logs          *
 *                     *
 ***********************/
extern "C" void dump_slab(meta_block const * slab) {
    pthread_lock_quard lock(MTX);

    printf("Slab [%p][%lu]\n", slab, (uint64_t)slab);
    if (slab == nullptr)
        return;

    printf("Next slab [%p][%lu]\n", slab->next, (uint64_t)slab->next);
    printf("List of free blocks (%zu):\n", slab->cnt_objects);

    size_t idx = 1;
    data_block * data = slab->head;

    while (data != nullptr) {
        printf("\t[%zu][%p][%lu]\n", idx, data, (uint64_t)data);
        data = data->next;
        idx++;
    }
}
extern "C" void dump_cache(struct cache const * cache) {
    assert(cache != nullptr);
    pthread_lock_quard lock(MTX);

    printf("Cache [%p][%lu]\n", cache, (uint64_t)cache);
    printf("\tslab_order=%d\n", cache->slab_order);
    printf("\tobject_size=%zu\n", cache->object_size);
    printf("\tcnt_objects=%zu\n", cache->cnt_objects);
    printf("\tmeta_block_offset=%zu\n", cache->meta_block_offset);
    printf("\tfree_list_slabs\t[%p]\n", cache->free_list_slabs);
    printf("\tbusy_list_slabs\t[%p]\n", cache->busy_list_slabs);
    printf("\tpart_list_slabs\t[%p]\n", cache->partbusy_list_slabs);
}


/***********************
 * Support handlers    *
 *                     *
 ***********************/
static meta_block * slab_setup(struct cache *cache) {
    assert(cache != nullptr);

    void * slab_ptr = alloc_slab(cache->slab_order);
    assert(slab_ptr != nullptr);

    size_t offset = cache->meta_block_offset;
    meta_block * meta = (meta_block *)((uint8_t *)slab_ptr + offset);
    meta->next = nullptr;
    meta->head = (data_block *)slab_ptr;
    meta->cnt_objects = cache->cnt_objects;

    uint8_t * base = (uint8_t *)meta->head;

    while (offset > cache->object_size) {
        data_block * curr_block = (data_block *)(base);
        data_block * next_block = (data_block *)(base + cache->object_size);

        curr_block->next = next_block;
        base = base + cache->object_size;
        offset -= cache->object_size;
    }

    ((data_block *)base)->next = nullptr;
    return meta;
}
static meta_block * slab_pop(struct cache *cache, SlabType type) {
    assert(cache != nullptr);
    meta_block * ret_slab = nullptr;

    switch (type) {
        case SlabType::FREE:
            ret_slab = cache->free_list_slabs;
            cache->free_list_slabs = cache->free_list_slabs->next;
            break;
        case SlabType::BUSY:
            ret_slab = cache->busy_list_slabs;
            cache->busy_list_slabs = cache->busy_list_slabs->next;
            break;
        case SlabType::PARTBUSY:
            ret_slab = cache->partbusy_list_slabs;
            cache->partbusy_list_slabs = cache->partbusy_list_slabs->next;
            break;
    }

    return ret_slab;
}
static void slab_push(struct cache *cache, meta_block * block, SlabType type) {
    assert(cache != nullptr && block != nullptr);

    switch (type) {
        case SlabType::FREE:
            block->next = cache->free_list_slabs;
            cache->free_list_slabs = block;
            break;
        case SlabType::BUSY:
            block->next = cache->busy_list_slabs;
            cache->busy_list_slabs = block;
            break;
        case SlabType::PARTBUSY:
            block->next = cache->partbusy_list_slabs;
            cache->partbusy_list_slabs = block;
            break;
    }
}
static void list_slabs_release(meta_block * block, size_t offset) {
    while (block != nullptr) {
        void * slab = (uint8_t *)block - offset;
        block = block->next;
        free_slab(slab);
    }
}
static pair<meta_block *,meta_block *> slab_find(meta_block * block, meta_block * root) {
    meta_block * prev = nullptr;
    meta_block * curr = root;

    while (curr != nullptr) {
        if (curr == block)
            return make_pair(prev, curr);
        prev = curr;
        curr = curr->next;
    }

    return make_pair(nullptr, nullptr);
}


/***********************
 *          API        *
 *                     *
 ***********************/

/**
 * It first initialize struct cache per O(1).
 * cache must be valid pointer on exist structure
 *
 * \param cache - structure, which need initialize
 * \object_size - size which you want allocate (must be > 0)
 **/
extern "C" void cache_setup(struct cache *cache, size_t object_size, int slab_order = 10) {
    assert(cache != nullptr && object_size > 0);
    pthread_lock_quard lock(MTX);

    cache->object_size  = object_size + DATA_BLOCK_SIZE;
    cache->slab_order   = slab_order;

    const size_t SLAB_SIZE = PAGE_SIZE * (1 << cache->slab_order); // 4 MiB
    cache->cnt_objects  = SLAB_SIZE / cache->object_size;

    while (SLAB_SIZE - cache->cnt_objects * cache->object_size < META_BLOCK_SIZE)
        cache->cnt_objects--;
    assert(cache->cnt_objects > 0);

    cache->meta_block_offset = cache->cnt_objects * cache->object_size;
    cache->free_list_slabs = slab_setup(cache);
}
/**
 * It deallocates all slabs (by free_slab)
 * and fill struct cache by zero per O(K),
 * K - count of slabs.
 * cache must be valid structure, otherwise undefined behavior
 **/
extern "C" void cache_release(struct cache *cache) {
    assert(cache != nullptr);
    pthread_lock_quard lock(MTX);

    list_slabs_release(cache->free_list_slabs, cache->meta_block_offset);
    list_slabs_release(cache->busy_list_slabs, cache->meta_block_offset);
    list_slabs_release(cache->partbusy_list_slabs, cache->meta_block_offset);
    *cache = {0,0,0,0,nullptr,nullptr,nullptr};
}
/**
 * It allocate one block of memory >= object_size per O(1).
 * cache must be valid structure, otherwise undefined behavior.
 *
 * \return pointer to memory
 **/
extern "C" void *cache_alloc(struct cache *cache) {
    assert(cache != nullptr);

    pthread_lock_quard lock(MTX);
    data_block * free_block = nullptr;

    if (cache->partbusy_list_slabs != nullptr) {
        free_block = cache->partbusy_list_slabs->head;
        cache->partbusy_list_slabs->head = free_block->next;
        cache->partbusy_list_slabs->cnt_objects--;

        if (free_block->next == nullptr) {
            meta_block * new_busy_block = slab_pop(cache, SlabType::PARTBUSY);
            slab_push(cache, new_busy_block, SlabType::BUSY);
        }
    } else if (cache->free_list_slabs != nullptr) {
        free_block = cache->free_list_slabs->head;
        cache->free_list_slabs->head = cache->free_list_slabs->head->next;
        cache->free_list_slabs->cnt_objects--;

        meta_block * new_busy_block = slab_pop(cache, SlabType::FREE);
        if (free_block->next == nullptr)
            slab_push(cache, new_busy_block, SlabType::BUSY);
        else
            slab_push(cache, new_busy_block, SlabType::PARTBUSY);
    } else {
        meta_block * new_free_block = slab_setup(cache);

        if (new_free_block != nullptr) {
            slab_push(cache, new_free_block, SlabType::FREE);

            lock.manual_unlock();
            return cache_alloc(cache);
        }
    }

    if (free_block != nullptr) {
        free_block->next = nullptr;
        return ((uint8_t *)free_block + DATA_BLOCK_SIZE);
    } else {
        return nullptr;
    }
}
/**
 * It come back one block into slab per O(1*).
 *
 * \param ptr - pointer to allocated memory before.
 * If is not valid pointer - undefined behavior
 **/
extern "C" void cache_free(struct cache *cache, void *ptr) {
    pthread_lock_quard lock(MTX);

    const int shift = PAGE_SIZE_DEGREE_2 + cache->slab_order;

    size_t aligment_numptr = (((size_t)ptr >> shift) << shift);

    data_block * dblock = (data_block *)((uint8_t *)ptr - DATA_BLOCK_SIZE);
    meta_block * mblock = (meta_block *)(aligment_numptr + cache->meta_block_offset);

    dblock->next = mblock->head;
    mblock->head = dblock;
    mblock->cnt_objects++;

    if (mblock->cnt_objects == 1) {
        auto [prev, curr] = slab_find(mblock, cache->busy_list_slabs);
        assert(curr != nullptr);

        if (prev != nullptr) {
            prev->next = curr->next;
            curr->next = cache->busy_list_slabs;
            cache->busy_list_slabs = curr;
        }

        if (mblock->cnt_objects == cache->cnt_objects)
            slab_push(cache, slab_pop(cache, SlabType::BUSY), SlabType::FREE);
        else
            slab_push(cache, slab_pop(cache, SlabType::BUSY), SlabType::PARTBUSY);
    } else if (mblock->cnt_objects == cache->cnt_objects) {
        auto [prev, curr] = slab_find(mblock, cache->partbusy_list_slabs);
        assert(curr != nullptr);

        if (prev != nullptr) {
            prev->next = curr->next;
            curr->next = cache->partbusy_list_slabs;
            cache->partbusy_list_slabs = curr;
        }

        slab_push(cache, slab_pop(cache, SlabType::PARTBUSY), SlabType::FREE);
    }
}
/**
 * It release all free slabs, if such exist
 **/
extern "C" void cache_shrink(struct cache *cache) {
    pthread_lock_quard lock(MTX);

    list_slabs_release(cache->free_list_slabs, cache->meta_block_offset);
    cache->free_list_slabs = nullptr;
}




/***********************
 *      Tests          *
 *                     *
 ***********************/

static struct cache mycache_alloc;
static const size_t object_size = (1 << 20); // 1 MiB

extern "C" void * routine(void * arg) {
    (void) arg;
    const size_t cnt_mall = 50;
    // summary = 50 MiB

    void * arr_ptrs[cnt_mall];

    for (size_t i = 0; i < cnt_mall; i++) {
        arr_ptrs[i] = cache_alloc(&mycache_alloc);
        assert(arr_ptrs[i] != nullptr);

        uint32_t * buffer = (uint32_t *)arr_ptrs[i];

        for (size_t j = 0; j < object_size / sizeof(uint32_t); j++)
             buffer[j] = j;
        for (size_t j = 0; j < object_size / sizeof(uint32_t); j++)
            assert(buffer[j] == j);

        if (i % 2 == 0)
            cache_free(&mycache_alloc, arr_ptrs[i]);
    }

    for (size_t i = 0; i < cnt_mall; i++)
        if (i % 2 != 0)
            cache_free(&mycache_alloc, arr_ptrs[i]);

    return NULL;
}

int main() {
    // test on race condition
    const int cnt_th = 10;
    pthread_t pool_th[cnt_th];

    cache_setup(&mycache_alloc, object_size);

    for (int i = 0; i < cnt_th; i++)
        pthread_create(&pool_th[i], NULL, &routine, NULL);

    for (int i = 0; i < cnt_th; i++)
        pthread_join(pool_th[i], NULL);

    cache_release(&mycache_alloc);

    // how use 'dump_slab' and 'dump_cache' for debug
    cache_setup(&mycache_alloc, object_size);

    dump_cache(&mycache_alloc);

    printf("Free slab state:\n");
        dump_slab(mycache_alloc.free_list_slabs);
    printf("\n");

    printf("Partially busy slab state:\n");
        dump_slab(mycache_alloc.partbusy_list_slabs);
    printf("\n");

    void * ptr1 = cache_alloc(&mycache_alloc);
    void * ptr2 = cache_alloc(&mycache_alloc);

    printf("Free slab state:\n");
        dump_slab(mycache_alloc.free_list_slabs);
    printf("\n");

    printf("Partially busy slab state:\n");
        dump_slab(mycache_alloc.partbusy_list_slabs);
    printf("\n");

    cache_free(&mycache_alloc, ptr1);
    cache_free(&mycache_alloc, ptr2);

    printf("Free slab state:\n");
        dump_slab(mycache_alloc.free_list_slabs);
    printf("\n");

    printf("Partially busy slab state:\n");
        dump_slab(mycache_alloc.partbusy_list_slabs);
    printf("\n");

    cache_release(&mycache_alloc);
    return 0;
}
