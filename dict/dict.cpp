#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/time.h>
#include <ctype.h>
#include <cassert>

#include "dict.h"
#include "zmalloc.h"

static int dict_can_resize = 1;
static unsigned int dict_force_resize_ratio = 5;
static uint32_t dict_hash_function_seed = 5381;

/*private prototypes*/
static int _dictExpandIfNeeded(dict *ht);
static unsigned long _dictNextPower(unsigned long size);
static int _dictKeyIndex(dict *ht, const void *key);
static int _dictInit(dict *d, dictType *type, void *privDataPtr);

static void _dictReset(dictht *ht)
{
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

int _dictInit(dict *d, dictType *type, void *privDataPtr)
{
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);
    d->type = type;
    d->privdata = privDataPtr;
    d->rehashidx = -1;
    d->iterators = 0;
    return DICT_OK;
}

int _dictClear(dict *d, dictht *ht, void(callback)(void *))
{
    unsigned long i;
    for(i = 0; i < ht->size && ht->used > 0; i++){
        dictEntry *he, *nextHe;
        if(callback && (i & 65535) == 0) callback(d->privdata);
        if((he = ht->table[i]) == NULL) continue;
        while(he){
            nextHe = he->next;
            dictFreeKey(d, he);
            dictFreeVal(d, he);
            zfree(he);
            ht->used--;
            he = nextHe;
        }
    }
    zfree(ht->table);
    _dictReset(ht);
    return DICT_OK;
}

int dictRehash(dict *d, int n)
{
    if(!dictIsRehashing(d)) return 0;
    while(n--){
        dictEntry *de, *nextde;
        if(d->ht[0].used == 0){
            zfree(d->ht[0].table);
            d->ht[0] = d->ht[1];
            _dictReset(&d->ht[1]);
            d->rehashidx = -1;
            return 0;
        }
        assert(d->ht[0].size > (unsigned)d->rehashidx);
        while(d->ht[0].table[d->rehashidx] == NULL) d->rehashidx++;
        de = d->ht[0].table[d->rehashidx];
        while(de){
            unsigned int h;
            nextde = de->next;
            h = dictHashKey(d, de->key) & d->ht[1].sizemask;
            de->next = d->ht[1].table[h];
            d->ht[1].table[h] = de;
            d->ht[0].used--;
            d->ht[1].used++;
            de = nextde;
        }
        d->ht[0].table[d->rehashidx] = NULL;
        d->rehashidx++;
    }
}

static void _dictRehashStep(dict *d)
{
    if(d->iterators == 0) dictRehash(d, 1);
}

long long dictFingerprint(dict *d)
{
    long long integers[6], hash = 0;
    int j;
    integers[0] = (long)d->ht[0].table;
    integers[1] = d->ht[0].size;
    integers[2] = d->ht[0].used;
    integers[3] = (long)d->ht[1].table;
    integers[4] = d->ht[1].size;
    integers[5] = d->ht[1].used;

    for(j = 0; j < 6; j++){
        hash += integers[j];
        hash = (~hash) + (hash << 21);
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8);
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4);
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
    }
    return hash;
}

dict *dictCreate(dictType *type, void *privDataPtr)
{
    dict *d = zmalloc(sizeof(*d));
    _dictInit(d, type, privDataPtr);
    return d;
}

int dictAdd(dict *d, void *key, void *val)
{
    dictEntry *entry = dictAddRaw(d, key);
    if(!entry) return DICT_ERR;
    dictSetVal(d, entry, val);
    return DICT_OK;
}

dictEntry *dictAddRaw(dict *d, void *key)
{
    int index;
    dictEntry *entry;
    dictht *ht;
    if(dictIsRehashing(d)) _dictRehashStep(d);
    if((index = _dictKeyIndex(d, key)) == -1) return NULL;
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    entry = zmalloc(sizeof(*entry));
    entry->next = ht->table[index];
    ht->table[index] = entry;
    ht->used++;
    dictSetKey(d, entry, key);
    return entry;
}

static int _dictKeyIndex(dict *d, const void *key)
{
    unsigned int h, idx, table;
    dictEntry *he;
    if(_dictExpandIfNeeded(d) == DICT_ERR) return -1;
    h = dictHashKey(d, key);
    for(table = 0; table <=1; table++){
        idx = h & d->ht[table].sizemask;
        he = d->ht[table].table[idx];
        while(he){
            if(dictCompareKeys(d, key, he->key)) return -1;
            he = he->next;
        }
        if(!dictIsRehashing(d)) break;
    }
    return idx;
}

unsigned int dictGenHashFunction(const void *key)
{
    uint32_t seed = dict_hash_function_seed;
    const unsigned char *data = (const unsigned char *)key;
    uint32_t len = strlen(data);
    const uint32_t m = 0x5bd1e995;
    const int r = 24;
    uint32_t h = seed ^ len;
    while(len >= 4){
        uint32_t k = *(uint32_t*)data;
        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len -= 4;
    }
    switch(len){
        case 3: h ^= data[2] << 16;
        case 2: h ^= data[1] << 8;
        case 1: h ^= data[0];h *= m;
    };
    h ^= h >>13;
    h *= m;
    h ^= h >> 15;
    return (unsigned int)h;
}

static int _dictExpandIfNeeded(dict *d)
{
    if(dictIsRehashing(d)) return DICT_OK;

    if(d->ht[0].size == 0) return dictExpand(d, DICT_HT_INITIAL_SIZE);
    if(d->ht[0].used >= d->ht[0].size && (dict_can_resize || d->ht[0].used/d->ht[0].size > dict_force_resize_ratio))
    {
        return dictExpand(d, d->ht[0].used*2);
    }
    return DICT_OK;
}

static unsigned long _dictNextPower(unsigned long size)
{
    unsigned long i = DICT_HT_INITIAL_SIZE;
    if(size >= LONG_MAX) return LONG_MAX;
    while(1){
        if(i >= size) return i;
        i *= 2;
    }
}

static int dictGenericDelete(dict *d, const void *key, int nofree)
{
    unsigned int h, idx;
    dictEntry *he, *prevHe;
    int table;
    if(d->ht[0].size == 0) return DICT_ERR;
    if(dictIsRehashing(d)) _dictRehashStep(d);

    h = dictHashKey(d, key);
    for (table = 0; table <= 1; table++){
        idx = h & d->ht[table].sizemask;
        he = d->ht[table].table[idx];
        prevHe = NULL;
        while(he){
            if(dictCompareKeys(d, key, he->key)){
                if(prevHe){
                    prevHe->next = he->next;
                } else {
                    d->ht[table].table[idx] = he->next;
                }
                if(!nofree){
                    dictFreeKey(d, he);
                    dictFreeVal(d, he);
                }
                zfree(he);
                d->ht[table].used--;
                return DICT_OK;
            }
            prevHe = he;
            he = he->next;
        }
        if(!dictIsRehashing(d)) break;
    }
    return DICT_ERR;
}

int dictExpand(dict *d, unsigned long size)
{
    dictht n;
    unsigned long realsize = _dictNextPower(size);
    if(dictIsRehashing(d) || d->ht[0].used > size){
        return DICT_ERR;
    }
    n.size = realsize;
    n.sizemask = realsize - 1;
    n.table = zcalloc(realsize*sizeof(dictEntry*));
    n.used = 0;
    if(d->ht[0].table == NULL){
        d->ht[0] = n;
    } else{
        d->ht[1] = n;
        d->rehashidx = 0;
    }
    return DICT_OK;
}

dictEntry *dictFind(dict *d, const void *key)
{
    dictEntry *he;
    unsigned int h, idx, table;
    if(d->ht[0].size == 0) return NULL;
    if(dictIsRehashing(d)) _dictRehashStep(d);
    h = dictHashKey(d, key);
    for (table = 0; table <= 1; table++) {
        idx = h & d->ht[table].sizemask;
        he = d->ht[table].table[idx];
        while(he){
            if(dictCompareKeys(d, key, he->key)) return he;
            he = he->next;
        }
        if(!dictIsRehashing(d)) return NULL;
    }
    return NULL;
}

int dictReplace(dict *d, void *key, void *val)
{
    dictEntry *entry, auxentry;
    if(dictAdd(d, key, val) == DICT_OK) return 1;
    entry = dictFind(d, key);
    auxentry = *entry;
    dictSetVal(d, entry, val);
    dictFreeVal(d, &auxentry);
}

dictEntry *dictReplaceRaw(dict *d, void *key)
{
    dictEntry *entry = dictFind(d, key);
    return entry ? entry : dictAddRaw(d, key);
}

int dictDelete(dict *d, const void *key){
    return dictGenericDelete(d, key, 0);
}

int dictDeleteNoFree(dict *d, const void *key){
    return dictGenericDelete(d, key, 1);
}

void dictRelease(dict *d)
{
    _dictClear(d, &d->ht[0], NULL);
    _dictClear(d, &d->ht[1], NULL);
    zfree(d);
}

void *dictFetchValue(dict *d, const void *key)
{
    dictEntry *he = dictFind(d, key);
    return he ? dictGetVal(he) : NULL;
}

dictIterator *dictGetIterator(dict *d)
{
    dictIterator *iter = zmalloc(sizeof(*iter));
    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;
    return iter;
}

dictIterator *dictGetSafeIterator(dict *d)
{
    dictIterator *iter = dictGetIterator(d);
    iter->safe = 1;
    return iter;
}

dictEntry *dictNext(dictIterator *iter)
{
    while(1){
        if(iter->entry == NULL){
            dictht *ht = &iter->d->ht[iter->table];
            if(iter->index == -1 && iter->table == 0){
                if(iter->safe)
                    iter->d->iterators++;
                else
                    iter->fingerprint = dictFingerprint(iter->d);
            }
            iter->index++;
            if(iter->index >= (signed) ht->size){
                if(dictIsRehashing(iter->d) && iter->table == 0){
                    iter->table++;
                    iter->index = 0;
                    ht = &iter->d->ht[iter->table];
                } else {
                    break;
                }
            }
            iter->entry = ht->table[iter->index];
        } else {
            iter->entry = iter->nextEntry;
        }
        if(iter->entry){
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }
    return NULL;
}