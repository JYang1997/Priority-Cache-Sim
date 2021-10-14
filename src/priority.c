#include "PriorityCache.h"
#include "hist.h"
#include <math.h>
#include "pcg_variants.h"
#include "uthash.h"
#include "priority.h"
#include <assert.h>

/*********************************HC priority interface************************************************/

void* HC_initPriority(PriorityCache_t* cache, PriorityCache_Item_t* item) {

	HC_Priority_t* p = malloc(sizeof(HC_Priority_t));
	p->refCount = 0;
	p->enterTime = cache->clock;

	item->priority = p;
	return p;
} 

void HC_updatePriorityOnHit(PriorityCache_t* cache, PriorityCache_Item_t* item) {

	HC_Priority_t* p = (HC_Priority_t*)(item->priority);
	p->refCount++;
	// p->lastAccessTime = cache->clock;

}


void HC_updatePriorityOnEvict(PriorityCache_t* cache, PriorityCache_Item_t* item) {

}

PriorityCache_Item_t* HC_minPriorityItem(PriorityCache_t* cache, PriorityCache_Item_t* item1, PriorityCache_Item_t* item2) {

	assert(item1 != NULL);
	assert(item2 != NULL);
	double p1, p2;
	HC_Priority_t* pp1 = (HC_Priority_t*)(item1->priority);
	HC_Priority_t* pp2 = (HC_Priority_t*)(item2->priority);
	p1 = (pp1->refCount) / (double)((cache->clock - pp1->enterTime)*(item1->size));
	p2 = (pp2->refCount) / (double)((cache->clock - pp2->enterTime)*(item2->size));

	return p1 <= p2 ? item1 : item2;
}



/*********************************PHC priority interface************************************************/
/***************perfect HC: use perfect freq and time from last access time******************************/

void PHC_globalDataInit(PriorityCache_t* cache) {
	PHC_globalData* gd = malloc(sizeof(PHC_globalData));

	gd->EvictedItem_HashTable = NULL;

	cache->globalData = (void*)gd;
}

void PHC_globalDataFree(PriorityCache_t* cache) {
	PHC_globalData* gd = (PHC_globalData*)cache->globalData;
	PHC_freqNode *currItem, *tmp;

	HASH_ITER(freq_hh, gd->EvictedItem_HashTable, currItem, tmp) {
		HASH_DELETE(freq_hh,gd->EvictedItem_HashTable, currItem);  /* delete; users advances to next */
		free(currItem);           /* optional- if you want to free  */
	}
}

//on init check whether it is in evicted table
// if it is, use the old freq cnt
void* PHC_initPriority(PriorityCache_t* cache, PriorityCache_Item_t* item) {
	

	PHC_Priority_t* p = malloc(sizeof(PHC_Priority_t));
	
	PHC_freqNode* temp;
	HASH_FIND(freq_hh, ((PHC_globalData*)(cache->globalData))->EvictedItem_HashTable, &(item->key), sizeof(uint64_t), temp);
	if (temp == NULL) {
		p->freqCnt = 1;

	} else {
		p->freqCnt = temp->freqCnt+1;
	}
	
	p->lastAccessTime = cache->clock;

	item->priority = p;
	return p;
} 

void PHC_updatePriorityOnHit(PriorityCache_t* cache, PriorityCache_Item_t* item) {
	PHC_Priority_t* p = (PHC_Priority_t*)(item->priority);
	p->freqCnt +=1;
	p->lastAccessTime = cache->clock;
	// printf("up%ld\n", p->freqCnt);
}

void PHC_updatePriorityOnEvict(PriorityCache_t* cache, PriorityCache_Item_t* item) {
//dup Item then store it to 
	PHC_freqNode* temp;
	HASH_FIND(freq_hh, ((PHC_globalData*)(cache->globalData))->EvictedItem_HashTable, &(item->key), sizeof(uint64_t), temp);
	if (temp == NULL) {
		temp = malloc(sizeof(PHC_freqNode));
		temp->key = item->key;
		HASH_ADD(freq_hh, ((PHC_globalData*)(cache->globalData))->EvictedItem_HashTable, key, sizeof(uint64_t), temp);
	}

	temp->freqCnt = ((PHC_Priority_t*)(item->priority))->freqCnt;
}



PriorityCache_Item_t* PHC_minPriorityItem(PriorityCache_t* cache, PriorityCache_Item_t* item1, PriorityCache_Item_t* item2) {
	
	assert(item1 != NULL);
	assert(item2 != NULL);

	PHC_Priority_t* pp1 = (PHC_Priority_t*)(item1->priority);
	PHC_Priority_t* pp2 = (PHC_Priority_t*)(item2->priority);
	double p1 = pp1->freqCnt / (double)(cache->clock - pp1->lastAccessTime);
	double p2 = pp2->freqCnt / (double)(cache->clock - pp2->lastAccessTime);
	// if (p1 == p2) return pp1->lastAccessTime <= pp2->lastAccessTime ? item1 : item2;
	return p1 <= p2 ? item1 : item2;
}


/*********************************LHD priority interface************************************************/



void LHD_globalDataInit(PriorityCache_t* cache) {
	LHD_globalData* gd = malloc(sizeof(LHD_globalData));

	gd->hitHist = histInit(81920, 128);
	gd->lifeTimeHist = histInit(81920, 128);
	gd->lhdHist = histInit(81920, 128);

	gd->lastUpdateTime = 0;
	gd->lhd_period = 100000;

	cache->globalData = (void*)gd;
}

void* LHD_initPriority(PriorityCache_t* cache, PriorityCache_Item_t* item) {
	LHD_globalData* gd = (LHD_globalData*) cache->globalData;

	LHD_Priority_t* p = malloc(sizeof(LHD_Priority_t));
	p->lastAccessTime = cache->clock;
	addToHist(gd->hitHist, COLDMISS);
	addToHist(gd->lifeTimeHist, COLDMISS);


	item->priority = p;
	return p;
} 

void LHD_updatePriorityOnHit(PriorityCache_t* cache, PriorityCache_Item_t* item) {
	
	LHD_globalData* gd = (LHD_globalData*) cache->globalData;

	LHD_Priority_t* p = (LHD_Priority_t*)(item->priority);

	uint64_t age = (cache->clock)-(p->lastAccessTime);
	addToHist(gd->hitHist, age);
	addToHist(gd->lifeTimeHist, age);
	
	p->lastAccessTime = cache->clock;
}


void LHD_updatePriorityOnEvict(PriorityCache_t* cache, PriorityCache_Item_t* item) {
	
	LHD_globalData* gd = (LHD_globalData*) cache->globalData;

	LHD_Priority_t* p = (LHD_Priority_t*)(item->priority);
	uint64_t age = (cache->clock)-(p->lastAccessTime);
	addToHist(gd->lifeTimeHist, age);

}

void updateLHDHist(PriorityCache_t* cache) {
	
	
	LHD_globalData* gd = (LHD_globalData*) cache->globalData;

	assert(gd->lhdHist->histSize > 0);
	
	

	//include cold miss? or not
	double acc_Hit = gd->hitHist->sdHist[gd->lhdHist->histSize-1];
	double acc_lifetime = gd->lifeTimeHist->sdHist[gd->lhdHist->histSize-1];
	double exp_lifetime = acc_lifetime;

	gd->lhdHist->sdHist[gd->lhdHist->histSize-1] = (acc_Hit/(gd->hitHist->totalCnt))/(exp_lifetime/(gd->lifeTimeHist->totalCnt));

	for (int i = gd->lhdHist->histSize-2; i >= 0 ; --i)
	{

		gd->lhdHist->sdHist[i] = (acc_Hit/(gd->hitHist->totalCnt))/(exp_lifetime/(gd->lifeTimeHist->totalCnt));
		acc_Hit += gd->hitHist->sdHist[i];
		acc_lifetime += gd->lifeTimeHist->sdHist[i];
		exp_lifetime  += acc_lifetime;
	}
}

PriorityCache_Item_t* LHD_minPriorityItem(PriorityCache_t* cache, PriorityCache_Item_t* item1, PriorityCache_Item_t* item2) {
	
	assert(item1 != NULL);
	assert(item2 != NULL);
	
	LHD_globalData* gd = (LHD_globalData*) cache->globalData;
	//check when is last time update the table
	//if 10000 reference passed, update it
	if (cache->clock - gd->lhd_period > gd->lastUpdateTime){
		gd->lastUpdateTime = cache->clock;
		updateLHDHist(cache);
	//	printf("%lld\n", cache->totRef);
	}

	double p1, p2;
	LHD_Priority_t* pp1 = (LHD_Priority_t*)(item1->priority);
	LHD_Priority_t* pp2 = (LHD_Priority_t*)(item2->priority);

	//evict the one with low priority
	uint64_t age1 = (cache->clock)-(pp1->lastAccessTime);
	uint64_t age2 = (cache->clock)-(pp2->lastAccessTime);

	int lhd_index1 = (((int)age1 - gd->lhdHist->interval) / (gd->lhdHist->interval)) + 1;
	int lhd_index2 = (((int)age2 - gd->lhdHist->interval) / (gd->lhdHist->interval)) + 1;

	if(lhd_index1 > gd->lhdHist->histSize-1) {
		lhd_index1 = gd->lhdHist->histSize-1;
	}

	if(lhd_index2 > gd->lhdHist->histSize-1) {
		lhd_index2 = gd->lhdHist->histSize-1;
	}

	//both mul hist step = not
	//p1 = gd->lhdHist->Hist[lhd_index1]/(histstep*(item1->size));
	//p2 = gd->lhdHist->Hist[lhd_index2]/(histstep*(item2->size));

	p1 = gd->lhdHist->sdHist[lhd_index1]/(item1->size);
	p2 = gd->lhdHist->sdHist[lhd_index2]/(item2->size);

	//printf("age1: %d p1 %f age2: %d p2 %f tot: %lld\n",age1, p1, age2, p2,  cache->totRef);
	return p1 <= p2 ? item1 : item2;
}


/*********************************LRU priority interface************************************************/

void* LRU_initPriority(PriorityCache_t* cache, PriorityCache_Item_t* item) {
	LRU_Priority_t* p = malloc(sizeof(LRU_Priority_t));
	p->lastAccessTime = cache->clock;


	item->priority = p;
	return p;
} 

void LRU_updatePriorityOnHit(PriorityCache_t* cache, PriorityCache_Item_t* item) {
	LRU_Priority_t* p = (LRU_Priority_t*)(item->priority);
	p->lastAccessTime = cache->clock;
}


void LRU_updatePriorityOnEvict(PriorityCache_t* cache, PriorityCache_Item_t* item) {
}



PriorityCache_Item_t* LRU_minPriorityItem(PriorityCache_t* cache, PriorityCache_Item_t* item1, PriorityCache_Item_t* item2) {
	
	assert(item1 != NULL);
	assert(item2 != NULL);


	LRU_Priority_t* pp1 = (LRU_Priority_t*)(item1->priority);
	LRU_Priority_t* pp2 = (LRU_Priority_t*)(item2->priority);
	uint64_t p1 = pp1->lastAccessTime;
	uint64_t p2 = pp2->lastAccessTime;
	return p1 <= p2 ? item1 : item2;
}


/*********************************MRU priority interface************************************************/


void* MRU_initPriority(PriorityCache_t* cache, PriorityCache_Item_t* item) {
	MRU_Priority_t* p = malloc(sizeof(MRU_Priority_t));
	p->lastAccessTime = cache->clock;


	item->priority = p;
	return p;
} 

void MRU_updatePriorityOnHit(PriorityCache_t* cache, PriorityCache_Item_t* item) {
	MRU_Priority_t* p = (MRU_Priority_t*)(item->priority);
	p->lastAccessTime = cache->clock;
}


void MRU_updatePriorityOnEvict(PriorityCache_t* cache, PriorityCache_Item_t* item) {
}


//only diffference between LRU is this comparison function
PriorityCache_Item_t* MRU_minPriorityItem(PriorityCache_t* cache, PriorityCache_Item_t* item1, PriorityCache_Item_t* item2) {
	
	assert(item1 != NULL);
	assert(item2 != NULL);

	MRU_Priority_t* pp1 = (MRU_Priority_t*)(item1->priority);
	MRU_Priority_t* pp2 = (MRU_Priority_t*)(item2->priority);
	uint64_t p1 = pp1->lastAccessTime;
	uint64_t p2 = pp2->lastAccessTime;
	return p1 > p2 ? item1 : item2;
}


/*********************************in cache LFU priority interface************************************************/

void* In_Cache_LFU_initPriority(PriorityCache_t* cache, PriorityCache_Item_t* item) {
	In_Cache_LFU_Priority_t* p = malloc(sizeof(In_Cache_LFU_Priority_t));
	p->freqCnt = 1;
	p->lastAccessTime = cache->clock;


	item->priority = p;
	return p;
} 

void In_Cache_LFU_updatePriorityOnHit(PriorityCache_t* cache, PriorityCache_Item_t* item) {
	In_Cache_LFU_Priority_t* p = (In_Cache_LFU_Priority_t*)(item->priority);
	p->lastAccessTime = cache->clock;
	p->freqCnt +=1;
}

void In_Cache_LFU_updatePriorityOnEvict(PriorityCache_t* cache, PriorityCache_Item_t* item) {
}



PriorityCache_Item_t* In_Cache_LFU_minPriorityItem(PriorityCache_t* cache, PriorityCache_Item_t* item1, PriorityCache_Item_t* item2) {
	
	assert(item1 != NULL);
	assert(item2 != NULL);

	In_Cache_LFU_Priority_t* pp1 = (In_Cache_LFU_Priority_t*)(item1->priority);
	In_Cache_LFU_Priority_t* pp2 = (In_Cache_LFU_Priority_t*)(item2->priority);
	uint64_t p1 = pp1->freqCnt;
	uint64_t p2 = pp2->freqCnt;
	if (p1 == p2) return pp1->lastAccessTime > pp2->lastAccessTime ? item1 : item2;
	return p1 < p2 ? item1 : item2;
}

/****************************perfect lfu priority interface************************************************/
void Perfect_LFU_globalDataInit(PriorityCache_t* cache) {
	Perfect_LFU_globalData* gd = malloc(sizeof(Perfect_LFU_globalData));

	gd->EvictedItem_HashTable = NULL;

	cache->globalData = (void*)gd;
}

void Perfect_LFU_globalDataFree(PriorityCache_t* cache) {
	Perfect_LFU_globalData* gd = (Perfect_LFU_globalData*)cache->globalData;
	Perfect_LFU_freqNode *currItem, *tmp;

	HASH_ITER(freq_hh, gd->EvictedItem_HashTable, currItem, tmp) {
		HASH_DELETE(freq_hh,gd->EvictedItem_HashTable, currItem);  /* delete; users advances to next */
		free(currItem);           /* optional- if you want to free  */
	}
}

//on init check whether it is in evicted table
// if it is, use the old freq cnt
void* Perfect_LFU_initPriority(PriorityCache_t* cache, PriorityCache_Item_t* item) {
	

	Perfect_LFU_Priority_t* p = malloc(sizeof(Perfect_LFU_Priority_t));
	
	Perfect_LFU_freqNode* temp;
	HASH_FIND(freq_hh, ((Perfect_LFU_globalData*)(cache->globalData))->EvictedItem_HashTable, &(item->key), sizeof(uint64_t), temp);
	if (temp == NULL) {
		p->freqCnt = 1;

	} else {
		p->freqCnt = temp->freqCnt+1;
	}
	p->lastAccessTime = cache->clock;

	item->priority = p;
	return p;
} 

void Perfect_LFU_updatePriorityOnHit(PriorityCache_t* cache, PriorityCache_Item_t* item) {
	Perfect_LFU_Priority_t* p = (Perfect_LFU_Priority_t*)(item->priority);
	p->lastAccessTime = cache->clock;
	p->freqCnt +=1;
	// printf("up%ld\n", p->freqCnt);
}

void Perfect_LFU_updatePriorityOnEvict(PriorityCache_t* cache, PriorityCache_Item_t* item) {
//dup Item then store it to 
	Perfect_LFU_freqNode* temp;
	HASH_FIND(freq_hh, ((Perfect_LFU_globalData*)(cache->globalData))->EvictedItem_HashTable, &(item->key), sizeof(uint64_t), temp);
	if (temp == NULL) {
		temp = malloc(sizeof(Perfect_LFU_freqNode));
		temp->key = item->key;
		HASH_ADD(freq_hh, ((Perfect_LFU_globalData*)(cache->globalData))->EvictedItem_HashTable, key, sizeof(uint64_t), temp);
	}

	temp->freqCnt = ((Perfect_LFU_Priority_t*)(item->priority))->freqCnt;
}



PriorityCache_Item_t* Perfect_LFU_minPriorityItem(PriorityCache_t* cache, PriorityCache_Item_t* item1, PriorityCache_Item_t* item2) {
	
	assert(item1 != NULL);
	assert(item2 != NULL);

	Perfect_LFU_Priority_t* pp1 = (Perfect_LFU_Priority_t*)(item1->priority);
	Perfect_LFU_Priority_t* pp2 = (Perfect_LFU_Priority_t*)(item2->priority);
	uint64_t p1 = pp1->freqCnt;
	uint64_t p2 = pp2->freqCnt;
	if (p1 == p2) return pp1->lastAccessTime > pp2->lastAccessTime ? item1 : item2;
	return p1 <= p2 ? item1 : item2;
}


/*********************************redis LFU priority interface************************************************/
//logarithmic counter
void* Redis_LFU_initPriority(PriorityCache_t* cache, PriorityCache_Item_t* item){
	Redis_LFU_Priority_t* p = malloc(sizeof(Redis_LFU_Priority_t));
	
	//redis original
	// p->access_time = (ustime()/1000000)/60 & (1<<REDIS_TIME_BIT);

	//change to logical
	p->access_time = (cache->clock/(REDIS_TIME_GRANULARITY)) & ((1 << REDIS_TIME_BIT)-1);

	p->freqCnt = REDIS_LFU_INIT_VAL;



	item->priority = p;

	return p;
}


//redis priority internal helper
static unsigned long Redis_LFUDecrAndReturn(PriorityCache_t* cache, PriorityCache_Item_t* item) {
	
	unsigned long ldt = ((Redis_LFU_Priority_t*)(item->priority))->access_time;
	unsigned long counter = ((Redis_LFU_Priority_t*)(item->priority))->freqCnt;


	//redis original
	//unsigned long now = (ustime()/1000000)/60 & 65536 ;
	//change to logical
	unsigned long now = (cache->clock/(REDIS_TIME_GRANULARITY)) & ((1 << REDIS_TIME_BIT)-1);

	unsigned long timeElapsed = now >= ldt ? now-ldt : ((1 << REDIS_TIME_BIT)-1)-ldt+now;

	unsigned long num_periods = REDIS_LFU_DECAY_TIME ? timeElapsed / REDIS_LFU_DECAY_TIME : 0;

	// if (num_periods)
    counter = (num_periods > counter) ? 0 : counter - num_periods;

    return counter;
}

//logic:
// on hit, decrement counter based on time lapsed //based on access time
//         then increment counter
//         lastly store incremented counter and currrent time 

void Redis_LFU_updatePriorityOnHit(PriorityCache_t* cache, PriorityCache_Item_t* item){
	
	unsigned long now = (cache->clock/(REDIS_TIME_GRANULARITY)) & ((1 << REDIS_TIME_BIT)-1);

    unsigned long counter = Redis_LFUDecrAndReturn(cache, item);
    //decrement complete, now start increment
    
	double r = ldexp(pcg64_random(), -64);
	double baseval = counter - REDIS_LFU_INIT_VAL;
	if (baseval < 0) baseval = 0;
	double p = 1.0/(baseval*REDIS_LFU_LOG_FACTOR+1);
    if (r < p) counter++;

    unsigned long long maxCnt = (1 << REDIS_FREQUENCY_BIT)-1;
	((Redis_LFU_Priority_t*)(item->priority))->freqCnt = counter > maxCnt ? maxCnt : counter;
    ((Redis_LFU_Priority_t*)(item->priority))->access_time = now;
}
void Redis_LFU_updatePriorityOnEvict(PriorityCache_t* cache, PriorityCache_Item_t* item){

}

PriorityCache_Item_t* Redis_LFU_minPriorityItem(PriorityCache_t* cache, PriorityCache_Item_t* item1, PriorityCache_Item_t* item2){

	unsigned long freqCnt1 = Redis_LFUDecrAndReturn(cache, item1);
	unsigned long freqCnt2 = Redis_LFUDecrAndReturn(cache, item2);

	unsigned long long maxCnt = (1 << REDIS_FREQUENCY_BIT)-1;

	assert(freqCnt1 <= maxCnt && freqCnt2 <= maxCnt);
	
	unsigned long t1 = ((Redis_LFU_Priority_t*)(item1->priority))->access_time;
	unsigned long t2 = ((Redis_LFU_Priority_t*)(item2->priority))->access_time;

	if (freqCnt1 == freqCnt2) return t1 <= t2 ? item2 : item1;
	return freqCnt1 < freqCnt2 ? item1 : item2;


}



