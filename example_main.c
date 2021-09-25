#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#include "PriorityCache.h"
#include "hist.h"
#include "priority.h"
#include <assert.h>

void cache_sim( char*     fileName,
                PriorityCache_t*     cache){

    FILE* rfd;
    if((rfd = fopen(fileName,"r")) == NULL)
    { perror("open error for read"); exit(-1); }


    char *keyStr;
    char *sizeStr;
    char *commandStr = NULL;
    uint32_t size;
    uint64_t key;

    char* ret;
    char   line[1024];
    

    uint64_t processedCnt = 0;
    while ((ret=fgets(line, 1024, rfd)) != NULL)
    {

        
        keyStr = strtok(line, ",");
        key = strtoull(keyStr, NULL, 10);
        sizeStr = strtok(NULL, ",");
        size = (sizeStr != NULL) ? strtoul(sizeStr, NULL, 10) : 1;
        commandStr = strtok(NULL, ",");
        commandStr = (commandStr == NULL) ? "GET" : commandStr;

        PC_access(cache, key, size, commandStr);
        
        if(processedCnt % 1000000 == 0){
            fprintf(stdout,"\rTotal Processed: %ld", processedCnt);  
            fflush(stdout);   
        }
        processedCnt++;

    }

}



int main(int argc, char const *argv[])
{

    char* input_format = "arg1: input filename\n\
arg2: Cache Size\n\
arg3: Sample Size\n\
arg4: policy(hc, lru, in-cache-lfu, perfect-lfu, lhd)\n\
This runnable accept trace format, \"key, size, command\"\n\
If size is not provided by default use logical size.\n";

    if(argc < 5) { 
        printf("%s", input_format);
        return 0;
    }

    char* input_path = strdup(argv[1]);


    PriorityCache_t *cache = cacheInit_auto(strtoul(argv[2], NULL, 10),
                                        strtoul(argv[3], NULL, 10),
                                        strdup(argv[4]));
    

    cache_sim(input_path, cache);

    output_results(cache, stdout);
    cacheFree_auto(cache);
    free(input_path);
    // fclose(rfd);
    return 0;
}

