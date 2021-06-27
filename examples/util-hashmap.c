
/*
 * Copyright (C) 2007 The Android Open Source Project
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
#include "util-hashmap.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifdef _MSC_VER
#define __inline__ __inline
#define inline __inline
#endif

#if !defined(_WIN32)
#include <pthread.h>
typedef pthread_mutex_t   mutex_t;
#define  MUTEX_INITIALIZER  PTHREAD_MUTEX_INITIALIZER
static __inline__ void  mutex_lock(mutex_t*  lock) {pthread_mutex_lock(lock);}
static __inline__ void  mutex_unlock(mutex_t*  lock) {pthread_mutex_unlock(lock);}
static __inline__ int  mutex_init(mutex_t*  lock) {return pthread_mutex_init(lock, NULL);}
static __inline__ void mutex_destroy(mutex_t*  lock) {pthread_mutex_destroy(lock);}
#else
#include <windows.h>
typedef struct {
    int                init;
    CRITICAL_SECTION   lock[1];
} mutex_t;
#define  MUTEX_INITIALIZER  { 0, {{ NULL, 0, 0, NULL, NULL, 0 }} }
static __inline__ void  mutex_lock(mutex_t*  lock)
{
    if (!lock->init) {
        lock->init = 1;
        InitializeCriticalSection( lock->lock );
        lock->init = 2;
    } else while (lock->init != 2)
        Sleep(10);
    EnterCriticalSection(lock->lock);
}
static __inline__ void  mutex_unlock(mutex_t*  lock)
{
    LeaveCriticalSection(lock->lock);
}
static __inline__ int  mutex_init(mutex_t*  lock)
{
    InitializeCriticalSection(lock->lock);
    lock->init = 2;
    return 0;
}
static __inline__ void  mutex_destroy(mutex_t*  lock)
{
    if (lock->init) {
        lock->init = 0;
        DeleteCriticalSection(lock->lock);
    }
}

#endif


typedef uint64_t hash_t;

typedef struct Entry Entry;
struct Entry {
    void* key;
    hash_t hash;
    void* value;
    Entry* next;
};
struct Hashmap {
    Entry** buckets;
    size_t bucketCount;
    uint64_t (*hash)(void* key);
    bool (*equals)(void* keyA, void* keyB);
    mutex_t lock;
    size_t size;
};
Hashmap* hashmapCreate(size_t initialCapacity,
        uint64_t (*hash)(void* key), bool (*equals)(void* keyA, void* keyB)) {
    Hashmap* map;
    size_t minimumBucketCount;

    assert(hash != NULL);
    assert(equals != NULL);
    
    map = malloc(sizeof(Hashmap));
    if (map == NULL) {
        return NULL;
    }
    
    // 0.75 load factor.
    minimumBucketCount = initialCapacity * 4 / 3;
    map->bucketCount = 1;
    while (map->bucketCount <= minimumBucketCount) {
        // Bucket count must be power of 2.
        map->bucketCount <<= 1;
    }
    map->buckets = calloc(map->bucketCount, sizeof(Entry*));
    if (map->buckets == NULL) {
        free(map);
        return NULL;
    }
    
    map->size = 0;
    map->hash = hash;
    map->equals = equals;
    
    mutex_init(&map->lock);
    
    return map;
}
/**
 * Hashes the given key.
 */
static inline hash_t _hash_from_key(Hashmap* map, void* key) {
    hash_t h = map->hash(key);
    // We apply this secondary hashing discovered by Doug Lea to defend
    // against bad hashes.
    h += ~(h << 9);
    h ^= (((unsigned int) h) >> 14);
    h += (h << 4);
    h ^= (((unsigned int) h) >> 10);
       
    return h;
}
size_t hashmapSize(Hashmap* map) {
    return map->size;
}
static inline size_t _index_from_hash(size_t bucketCount, hash_t hash) {
    return ((size_t) hash) & (bucketCount - 1);
}
static void expandIfNecessary(Hashmap* map) {
    // If the load factor exceeds 0.75...
    if (map->size > (map->bucketCount * 3 / 4)) {
        size_t i;

        // Start off with a 0.33 load factor.
        size_t newBucketCount = map->bucketCount << 1;
        Entry** newBuckets = calloc(newBucketCount, sizeof(Entry*));
        if (newBuckets == NULL) {
            // Abort expansion.
            return;
        }
        
        // Move over existing entries.
        for (i = 0; i < map->bucketCount; i++) {
            Entry* entry = map->buckets[i];
            while (entry != NULL) {
                Entry* next = entry->next;
                size_t index = _index_from_hash(newBucketCount, entry->hash);
                entry->next = newBuckets[index];
                newBuckets[index] = entry;
                entry = next;
            }
        }
        // Copy over internals.
        free(map->buckets);
        map->buckets = newBuckets;
        map->bucketCount = newBucketCount;
    }
}
void hashmapLock(Hashmap* map) {
    mutex_lock(&map->lock);
}
void hashmapUnlock(Hashmap* map) {
    mutex_unlock(&map->lock);
}
void hashmapFree(Hashmap* map) {
    size_t i;
    for (i = 0; i < map->bucketCount; i++) {
        Entry* entry = map->buckets[i];
        while (entry != NULL) {
            Entry* next = entry->next;
            free(entry);
            entry = next;
        }
    }
    free(map->buckets);
    mutex_destroy(&map->lock);
    free(map);
}
#ifdef __clang__
__attribute__((no_sanitize("integer")))
#endif
/* FIXME: relies on signed integer overflow, which is undefined behavior */
int hashmapHash(void* key, size_t keySize) {
    int h = (int)keySize;
    char* data = (char*) key;
    size_t i;
    for (i = 0; i < keySize; i++) {
        h = h * 31 + *data;
        data++;
    }
    return h;
}
static Entry* _create_entry(void* key, hash_t hash, void* value) {
    Entry* entry = malloc(sizeof(Entry));
    if (entry == NULL) {
        return NULL;
    }
    entry->key = key;
    entry->hash = hash;
    entry->value = value;
    entry->next = NULL;
    return entry;
}
static inline bool equalKeys(void* keyA, hash_t hashA, void* keyB, hash_t hashB,
        bool (*equals)(void*, void*)) {
    if (keyA == keyB) {
        return true;
    }
    if (hashA != hashB) {
        return false;
    }
    return equals(keyA, keyB);
}
void* hashmapPut(Hashmap* map, void* key, void* value) {
    uint64_t hash = _hash_from_key(map, key);
    size_t index = _index_from_hash(map->bucketCount, hash);
    Entry** p = &(map->buckets[index]);
    while (true) {
        Entry* current = *p;
        // Add a new entry.
        if (current == NULL) {
            *p = _create_entry(key, hash, value);
            if (*p == NULL) {
                errno = ENOMEM;
                return NULL;
            }
            map->size++;
            expandIfNecessary(map);
            return NULL;
        }
        // Replace existing entry.
        if (equalKeys(current->key, current->hash, key, hash, map->equals)) {
            void* oldValue = current->value;
            current->value = value;
            return oldValue;
        }
        // Move to next entry.
        p = &current->next;
    }
}
void* hashmapGet(Hashmap* map, void* key) {
    hash_t hash = _hash_from_key(map, key);
    size_t index = _index_from_hash(map->bucketCount, hash);
    Entry* entry = map->buckets[index];
    while (entry != NULL) {
        if (equalKeys(entry->key, entry->hash, key, hash, map->equals)) {
            return entry->value;
        }
        entry = entry->next;
    }
    return NULL;
}
bool hashmapContainsKey(Hashmap* map, void* key) {
    hash_t hash = _hash_from_key(map, key);
    size_t index = _index_from_hash(map->bucketCount, hash);
    Entry* entry = map->buckets[index];
    while (entry != NULL) {
        if (equalKeys(entry->key, entry->hash, key, hash, map->equals)) {
            return true;
        }
        entry = entry->next;
    }
    return false;
}
void* hashmapMemoize(Hashmap* map, void* key,
        void* (*initialValue)(void* key, void* context), void* context) {
    hash_t hash = _hash_from_key(map, key);
    size_t index = _index_from_hash(map->bucketCount, hash);
    Entry** p = &(map->buckets[index]);
    while (true) {
        Entry* current = *p;
        // Add a new entry.
        if (current == NULL) {
            void* value;

            *p = _create_entry(key, hash, NULL);
            if (*p == NULL) {
                errno = ENOMEM;
                return NULL;
            }
            value = initialValue(key, context);
            (*p)->value = value;
            map->size++;
            expandIfNecessary(map);
            return value;
        }
        // Return existing value.
        if (equalKeys(current->key, current->hash, key, hash, map->equals)) {
            return current->value;
        }
        // Move to next entry.
        p = &current->next;
    }
}
void* hashmapRemove(Hashmap* map, void* key) {
    hash_t hash = _hash_from_key(map, key);
    size_t index = _index_from_hash(map->bucketCount, hash);
    // Pointer to the current entry.
    Entry** p = &(map->buckets[index]);
    Entry* current;
    while ((current = *p) != NULL) {
        if (equalKeys(current->key, current->hash, key, hash, map->equals)) {
            void* value = current->value;
            *p = current->next;
            free(current);
            map->size--;
            return value;
        }
        p = &current->next;
    }
    return NULL;
}
void hashmapForEach(Hashmap* map,
        bool (*callback)(void* key, void* value, void* context),
        void* context) {
    size_t i;
    for (i = 0; i < map->bucketCount; i++) {
        Entry* entry = map->buckets[i];
        while (entry != NULL) {
            Entry *next = entry->next;
            if (!callback(entry->key, entry->value, context)) {
                return;
            }
            entry = next;
        }
    }
}
size_t hashmapCurrentCapacity(Hashmap* map) {
    size_t bucketCount = map->bucketCount;
    return bucketCount * 3 / 4;
}
size_t hashmapCountCollisions(Hashmap* map) {
    size_t collisions = 0;
    size_t i;
    for (i = 0; i < map->bucketCount; i++) {
        Entry* entry = map->buckets[i];
        while (entry != NULL) {
            if (entry->next != NULL) {
                collisions++;
            }
            entry = entry->next;
        }
    }
    return collisions;
}
int hashmapIntHash(void* key) {
    // Return the key value itself.
    return *((int*) key);
}
bool hashmapIntEquals(void* keyA, void* keyB) {
    int a = *((int*) keyA);
    int b = *((int*) keyB);
    return a == b;
}
