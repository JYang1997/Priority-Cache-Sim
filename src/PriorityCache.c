#include <stdlib.h> 
#include <stdio.h>
#include <time.h> 
#include <sys/time.h>
#include <math.h>
#include <assert.h>
#include "uthash.h"
#include "pcg_variants.h"
#include "entropy.h"
#include <string.h>
#include "PriorityCache.h"
#include "hist.h"
#include "priority.h"



//the capacity can be in arbitrary unit, makesure item size agree with it
PriorityCache_t* cacheInit(double cap, 
                       uint32_t sam,
                       char* name, 
                       CreatePriority pInit,
                       UpdatePriorityOnHit pUpdateOnHit,
                       UpdatePriorityOnEvict pUpdateOnEvict,
                       MinPriorityItem pMin
                       ){

    PriorityCache_t* PC = malloc(sizeof(PriorityCache_t));
    if (PC == NULL) {
        perror("PriorityCache init failed!\n");
        return PC;
    } 
    volatile pcg128_t seeds[2];
    fallback_entropy_getbytes((void*)seeds, sizeof(seeds));
    pcg64_srandom(seeds[0], seeds[1]);
    
    PC->currNum = 0;
    PC->capacity = cap;
    PC->currSize = 0;
    PC->sample_size = sam;
    PC->clock = 0;

    PC->policyName = strdup(name);
    PC->Item_HashTable = NULL;
    PC->ItemIndex_HashTable = NULL;

    PC->createPriority = pInit;
    PC->updatePriorityOnHit = pUpdateOnHit;
    PC->updatePriorityOnEvict = pUpdateOnEvict;
    PC->minPriorityItem = pMin;

    PC->stat = malloc(sizeof(PC_Stats_t));
    PC_statInit(PC->stat);
    return PC;
}

void PC_statInit(PC_Stats_t* stat) {
    stat->totRef = 0; //tot references to cache
    stat->totKey = 0; //tot Key in cache
    stat->totMiss = 0;
    stat->totGet = 0; 
    stat->totSet = 0;
    stat->totGetSet = 0;
    stat->totDel = 0;
    stat->totEvict = 0;
}

void cacheFree(PriorityCache_t* cache) {

  PriorityCache_Item_t *currItem, *tmp;

  HASH_ITER(key_hh, cache->Item_HashTable, currItem, tmp) {
    HASH_DELETE(pos_hh,cache->ItemIndex_HashTable, currItem);  /* delete; users advances to next */
    HASH_DELETE(key_hh,cache->Item_HashTable, currItem);
    free(currItem->priority);
    free(currItem);            /* optional- if you want to free  */
  }
  free(cache->policyName);
  free((void*)cache);
}



void evictItem(PriorityCache_t* cache) {

    assert(cache != NULL);

    PriorityCache_Item_t *temp1, *temp2, *min;
    uint64_t rand_index;
    uint64_t ret_index;

    
    while (1) { 
        rand_index = pcg64_random()%(cache->currNum); //randomly select one element
        temp1=NULL, temp2=NULL, min=NULL;
        HASH_FIND(pos_hh, cache->ItemIndex_HashTable, &rand_index, sizeof(uint64_t), temp1);
        assert(temp1 != NULL);
        //sampling
        min = temp1;
        for (int i = 1; i<cache->sample_size; i++) {
            rand_index = pcg64_random()%(cache->currNum);
            HASH_FIND(pos_hh, cache->ItemIndex_HashTable, &rand_index, sizeof(uint64_t), temp2);
            assert(temp2 != NULL);
            min = cache->minPriorityItem(cache, temp1, temp2);
            temp1 = min;
            temp2 = NULL;   
        }

        min = deleteItem(cache, min->key);
        free(min->priority);
        free(min);
        
        cache->stat->totEvict++;


        if ((cache->currSize + CAP_EPS) < cache->capacity) { 
            break;
        }

    }
}


PriorityCache_Item_t* deleteItem(PriorityCache_t* cache, uint64_t key) {
    assert(cache != NULL);

    PriorityCache_Item_t* item = getItem(cache, key);

    if (item != NULL) {

        cache->updatePriorityOnEvict(cache, item);
        uint64_t ret_index = item->index;
        HASH_DELETE(key_hh, cache->Item_HashTable, item);
        HASH_DELETE(pos_hh, cache->ItemIndex_HashTable, item);
        cache->currNum--;
        cache->currSize -= item->size;
        cache->stat->totKey--;

        PriorityCache_Item_t* temp = NULL;  //currNum already sub by 1,hence point to last element
        HASH_FIND(pos_hh, cache->ItemIndex_HashTable, &(cache->currNum), sizeof(uint64_t), temp);
 
        if (temp != NULL) {
            HASH_DELETE(pos_hh, cache->ItemIndex_HashTable, temp);

            temp->index = ret_index;
            HASH_ADD(pos_hh, cache->ItemIndex_HashTable, index, sizeof(uint64_t), temp);
        }
    } 
    
    return item;
}


void addItem(PriorityCache_t* cache, PriorityCache_Item_t* item) {
    assert(item != NULL && cache != NULL);

    //before adding, handle eviction
    cache->currSize += item->size;
    while (cache->currSize > cache->capacity) //eps handle
        evictItem(cache); //cache size reduce 


    //if eviction happen, item's index might need to change to most recent currNum
    item->index = cache->currNum;

    HASH_ADD(pos_hh, cache->ItemIndex_HashTable, index, sizeof(uint64_t), item);
    HASH_ADD(key_hh, cache->Item_HashTable, key, sizeof(uint64_t), item);
    cache->stat->totKey++;
    cache->currNum++;
}

//for a logical cache, size is just one
PriorityCache_Item_t* createItem (uint64_t key, 
                              uint64_t size, 
                              uint64_t index) {

    PriorityCache_Item_t *item = malloc(sizeof(PriorityCache_Item_t));
    if (item == NULL) {
        perror("item init failed!\n");
        return item;
    }
    item->key = key;
    item->index = index;
    item->size = size;
    item->priority = NULL;
    
    return item;
}


//internal helper
PriorityCache_Item_t* getItem(PriorityCache_t* cache, uint64_t key) {
    PriorityCache_Item_t* item = NULL;
    HASH_FIND(key_hh, cache->Item_HashTable, &key, sizeof(uint64_t), item);
    return item;
}


uint8_t setItem(PriorityCache_t* cache, uint64_t key, uint64_t size) {

    if (size > cache->capacity) {
          fprintf(stderr, "key: %ld size: %ld exceed cache capacity! \n", key, size);
        return SET_ERROR;
    }

    PriorityCache_Item_t* item = getItem(cache, key);

    if (item != NULL) {
        item = deleteItem(cache, item->key);
        free(item->priority);
        free(item);
    }
    
    item = createItem(key, size, cache->currNum);
    item->priority = cache->createPriority(cache, item);
    addItem(cache, item); //cache currNum (index) increment++

    return SET_SUCESS;
}



PriorityCache_Item_t* PC_get(PriorityCache_t* cache, uint64_t key) {

    assert(cache != NULL);

    cache->clock++;
    cache->stat->totRef++;
    cache->stat->totGet++;

    PriorityCache_Item_t* item = getItem(cache, key);

    if (item == NULL) cache->stat->totMiss += 1;
    else {
        cache->updatePriorityOnHit(cache, item);
    }
    return item;
}

uint8_t PC_set(PriorityCache_t* cache, uint64_t key, uint64_t size) {

    assert(cache != NULL);

    cache->clock++;
    cache->stat->totRef++;
    cache->stat->totSet++;

    uint8_t ret = setItem(cache, key, size);

    if (ret == SET_ERROR) {
        exit(-1);
    }

    return ret;

}

uint8_t PC_getAndSet(PriorityCache_t* cache, uint64_t key, uint64_t size) {

    assert(cache != NULL);
    cache->clock++;
    cache->stat->totRef++;
    cache->stat->totGetSet++;

    PriorityCache_Item_t* item = getItem(cache, key);

    if (item == NULL) {
        uint8_t ret = setItem(cache, key, size);
        if (ret == SET_ERROR) exit(-1);
        cache->stat->totMiss += 1;
        return CACHE_MISS;
    } else {
        cache->updatePriorityOnHit(cache, item);
        return CACHE_HIT;
    }

}

//when delete move index 
PriorityCache_Item_t* PC_del(PriorityCache_t* cache, uint64_t key) {
    assert(cache != NULL);

    cache->clock += 1;
    cache->stat->totRef += 1;
    cache->stat->totDel += 1;

    PriorityCache_Item_t* item = deleteItem(cache, key);
   
    return item;

}



void PC_access(PriorityCache_t*  cache, 
                      uint64_t      key,
                      uint64_t      size,
                      char*         commandStr) {

    

    if (strcmp(commandStr, "GET") == 0) {
        PC_getAndSet(cache, key, size);
    } else if (strcmp(commandStr, "SET") == 0) {
        PC_set(cache, key, size);
    } else if (strcmp(commandStr, "DELETE") == 0) {
        PriorityCache_Item_t* item = PC_del(cache, key);
        if (item != NULL) {
            free(item->priority);
            free(item);
        }
    } else if (strcmp(commandStr, "UPDATE") == 0) {
        PC_set(cache, key, size);
    } else {
        fprintf(stderr,"cache reference number: %ld, Invalid command %s\n",
            cache->stat->totRef, commandStr);
        exit(-1);
    }
    
}

void output_results(PriorityCache_t* cache, FILE* fd) {
    struct timeval tv;
    time_t t;
    struct tm *info;
    char buffer[64];

    gettimeofday(&tv, NULL);
    t = tv.tv_sec;

    info = localtime(&t);
    char *tstr = asctime(info);
    tstr[strlen(tstr) - 1] = 0;
    fprintf(fd,"%s ", tstr);

    fprintf(fd, "CacheSize: %0.4f "
                "SampleSize: %d "
                "Policy: %s "
                "missRate: %0.6f "
                "totRef: %ld "
                "totKey: %ld "
                "totMiss: %ld "
                "totGet: %ld "
                "totSet: %ld "
                "totGetSet: %ld "
                "totDel: %ld "
                "totEvict: %ld\n",
                cache->capacity,
                cache->sample_size,
                cache->policyName,
                cache->stat->totMiss/(double)(cache->stat->totGetSet+cache->stat->totGet),
                cache->stat->totRef,
                cache->stat->totKey,
                cache->stat->totMiss,
                cache->stat->totGet,
                cache->stat->totSet,
                cache->stat->totGetSet,
                cache->stat->totDel,
                cache->stat->totEvict);
}




PriorityCache_t* cacheInit_auto(double      cap, 
                            uint32_t    sam, 
                            char*       name) {
    PriorityCache_t* cache = NULL;

    if (strcmp(name, "lhd") == 0){
        //cache init
        //hist init temporary
        // hitHist = histInit(128,1024*1024*10, 128);
        // lifeTimeHist = histInit(128,1024*1024*10, 128);
        // lhdHist = histInit(128,1024*1024*10, 128);
        cache = cacheInit(cap,
                          sam,
                           "lhd",
                           LHD_initPriority,
                           LHD_updatePriorityOnHit,
                           LHD_updatePriorityOnEvict,
                           LHD_minPriorityItem);
        LHD_globalDataInit(cache);
    }else if (strcmp(name, "lru") == 0) {
        cache = cacheInit(cap,
                          sam,
                           "lru",
                           LRU_initPriority,
                           LRU_updatePriorityOnHit,
                           LRU_updatePriorityOnEvict,
                           LRU_minPriorityItem);
    }else if (strcmp(name, "mru") == 0) {
        cache = cacheInit(cap,
                          sam,
                           "mru",
                           MRU_initPriority,
                           MRU_updatePriorityOnHit,
                           MRU_updatePriorityOnEvict,
                           MRU_minPriorityItem); 
    }else if (strcmp(name, "in-cache-lfu") == 0) {
        cache = cacheInit(cap,
                          sam,
                           "in-cache-lfu",
                           In_Cache_LFU_initPriority,
                           In_Cache_LFU_updatePriorityOnHit,
                           In_Cache_LFU_updatePriorityOnEvict,
                           In_Cache_LFU_minPriorityItem);
    }else if (strcmp(name, "perfect-lfu") == 0) {
        cache = cacheInit(cap,
                          sam,
                           "perfect-lfu",
                           Perfect_LFU_initPriority,
                           Perfect_LFU_updatePriorityOnHit,
                           Perfect_LFU_updatePriorityOnEvict,
                           Perfect_LFU_minPriorityItem);
        Perfect_LFU_globalDataInit(cache);
    } else if (strcmp(name, "hc") == 0) {
        cache = cacheInit(cap,
                          sam,
                           "hc",
                           HC_initPriority,
                           HC_updatePriorityOnHit,
                           HC_updatePriorityOnEvict,
                           HC_minPriorityItem);
    } else if (strcmp(name, "phc") == 0) {
        cache = cacheInit(cap,
                          sam,
                           "phc",
                           PHC_initPriority,
                           PHC_updatePriorityOnHit,
                           PHC_updatePriorityOnEvict,
                           PHC_minPriorityItem);
            PHC_globalDataInit(cache);
    }else {
        printf("incorrect policy name\n");
        exit(-1);
    }

    return cache;
}


void cacheFree_auto(PriorityCache_t* cache) {

    if (strcmp(cache->policyName, "phc") == 0) {
        PHC_globalDataFree(cache);
    } else if (strcmp(cache->policyName, "perfect-lfu") == 0) {
        Perfect_LFU_globalDataFree(cache);
    } 

    cacheFree(cache);
}