//
//  main.c
//  nyuenc
//
//  Created by Minghao Sun on 10/21/22.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

typedef struct work{
    char* charArray;
    int workSize;
    int index;
} work;


//structure for finished works
typedef struct encodedWork{
    unsigned char* encoded;
    unsigned char last;
    unsigned char lastCount;
    int workSize;
    int workIndex;
} encodedWork;

pthread_mutex_t mutexForWorkList;
pthread_cond_t condForWorkList;
pthread_mutex_t mutexForFinishList;
pthread_cond_t condForFinishList;


//an array of finished work, and the main thread will use this 
//array to write info to the file
//There can be no more than 263144 4kB in 1GB
encodedWork **finishList;
work **workList;
int workFinished = 0;
int workTaken = 0;
int workReleased = 0;

int taskCount = 0;

void encoding(char* charArray, int size, int index){

    unsigned char* encoded = malloc(sizeof(char) * size * 2);
    unsigned char count = 0;
    int j = 0;
    int i;

    char previous = charArray[0];

    for (i = 0; i < size; i++){
        char current = charArray[i];
        if (current == previous){
            count++;
        }else{
            encoded[j] = previous;
            encoded[j+1] = count;
            previous = current;
            count = 1;
            j += 2;
        }
    }
    
    // free(charArray);
    struct encodedWork* newFinishedWork = malloc(sizeof(encodedWork*));
    (*newFinishedWork).encoded = encoded;
    (*newFinishedWork).last = previous;
    (*newFinishedWork).lastCount = count;
    (*newFinishedWork).workSize = j;


    (*newFinishedWork).workIndex = index;

    pthread_mutex_lock(&mutexForFinishList);
    finishList[(*newFinishedWork).workIndex] = newFinishedWork;
    workFinished++;
    pthread_mutex_unlock(&mutexForFinishList);
    pthread_cond_signal(&condForFinishList);
    
}


void writeConcat(int index, unsigned char* content, int size, unsigned char previousLast, unsigned char previousLastCount){
    
    if (index){
        if (content[0] == previousLast){
            content[1] += previousLastCount;
        }else{
            unsigned char lastTwo[2];
            lastTwo[0] = previousLast;
            lastTwo[1] = previousLastCount;
            write(1, lastTwo, 2);
        }
    }
    write(1, content, size);
    free(content);
}


void *startThread(void* args){
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    for(;;){
        struct work* currentWork;

        pthread_mutex_lock(&mutexForWorkList);

        // while (workReleased - workTaken == 0){
        if(workReleased - workTaken == 0){
            pthread_mutex_unlock(&mutexForWorkList);
            break;
        }

        currentWork = workList[workTaken];
        workTaken++;
        pthread_mutex_unlock(&mutexForWorkList);

        encoding((*currentWork).charArray, (*currentWork).workSize, (*currentWork).index);
    
    }
}

void submit(char* fileName){
    //referencing from: https://www.youtube.com/watch?v=m7E9piHcfr4
    //opens the file
    int inputFile = open(fileName, O_RDONLY);
    struct stat sb;
    if (fstat(inputFile, &sb) == -1){
        fprintf(stderr, "Error: unable to open file\n");
    }
    int remainder = sb.st_size % 4096;
    int workNum = sb.st_size / 4096 + 1;
    int workIndex;
    char* fileContent = malloc(sb.st_size);
    fileContent = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, inputFile, 0);
    for (workIndex = 0; workIndex < workNum; workIndex++){
        //mmap the content of the file
        int allocateSize = sizeof(char) * 4096;
        if (workIndex == workNum - 1){
            if (remainder){
                allocateSize = remainder;
            }else{
                continue;
            }
        }
        // if (workIndex == 17353){
        //     printf("hh");
        // }

        
        

        struct work* newWork = (struct work*) malloc(sizeof(work));
        struct work temp = {
            .charArray = fileContent,
            .workSize = allocateSize,
            .index = 0
        };
        *newWork = temp;
        fileContent += 4096;

        // (*newWork).charArray = fileContent;
        //cretical section
        pthread_mutex_lock(&mutexForWorkList);
        (*newWork).index = workReleased;
        workReleased++;
        workList[(*newWork).index] = newWork;
        // close(inputFile);
        pthread_mutex_unlock(&mutexForWorkList);
        pthread_cond_signal(&condForWorkList);
    }
}

void myWrite(){
    //the main thread will get finished work from the finishedWorkList based on the fIndex
    pthread_mutex_lock(&mutexForFinishList);
    while(finishList[0] == NULL){
        pthread_cond_wait(&condForFinishList, &mutexForFinishList);
    }
    pthread_mutex_unlock(&mutexForFinishList);
     write(1, (*finishList[0]).encoded, (*finishList[0]).workSize);
    if (workReleased == 1){
        return;
    }

    int fIndex = 1;
    while(1){
        pthread_mutex_lock(&mutexForFinishList);

        while(finishList[fIndex] == NULL){
            pthread_cond_wait(&condForFinishList, &mutexForFinishList);
        }
        struct encodedWork* current = finishList[fIndex];
        struct encodedWork* previous = finishList[fIndex-1];
        pthread_mutex_unlock(&mutexForFinishList);
        writeConcat(fIndex, (*current).encoded, (*current).workSize, (*previous).last, (*previous).lastCount);
        fIndex++;
        if (fIndex == workReleased){
            break;
        }
    }
    
}

int main(int argc, char * argv[]) {
    
    if (!workList){
        workList = (work **) malloc(sizeof(work*) * 263144);
    }

    if (!finishList){
        finishList = (encodedWork **) malloc(sizeof(encodedWork*) * 263144);
    }

    int j;
    for (j = 0; j < 263144; j++) {
        // workList[j] = NULL;
        finishList[j] = NULL;
    }

    int k = 1;
    int numOfThread = 0;
    //if there is -j, will start using thread
    if(strcmp(argv[1], "-j") == 0){
        numOfThread = atoi(argv[2]);
        k = 3;
    }

    pthread_mutex_init(&mutexForWorkList, NULL);
    pthread_cond_init(&condForWorkList, NULL);
    pthread_mutex_init(&mutexForFinishList, NULL);
    pthread_cond_init(&condForFinishList, NULL);

    while (argv[k]){
        submit(argv[k]);
        k++;
    }

    //the case when no thread is needed;
    if (numOfThread == 0){
        int wIndex;
        for (wIndex = 0; wIndex < workReleased; wIndex++){
            struct work* currentWork = workList[workTaken];
            workTaken++;
            encoding((*currentWork).charArray, (*currentWork).workSize, (*currentWork).index);
        }

        int fIndex;
        struct encodedWork* previous = finishList[0];
        for (fIndex = 0; fIndex < workTaken; fIndex++){
            struct encodedWork* current = finishList[fIndex];
            writeConcat(fIndex, (*current).encoded,(*current).workSize, (*previous).last, (*previous).lastCount);
            previous = current;
        }
        unsigned char lastTwo[2];
        lastTwo[0] = (*previous).last;
        lastTwo[1] = (*previous).lastCount;
        write(1, lastTwo, 2);
        return 0;
    }

    pthread_t th[numOfThread];
    int threadIndex;
    for (threadIndex = 0; threadIndex < numOfThread; threadIndex++){
        if (pthread_create(&th[threadIndex], NULL, &startThread, NULL) != 0){
            fprintf(stderr, "Error: cannot create thread\n");
        }
    }

   
    
    myWrite();

    for (threadIndex = 0; threadIndex < numOfThread; threadIndex++){
        pthread_cancel(th[threadIndex]);
    }

    for (threadIndex = 0; threadIndex < numOfThread; threadIndex++){
        if (pthread_join(th[threadIndex], NULL) != 0){
            fprintf(stderr, "Error: cannot join thread\n");
        }
    }
    pthread_mutex_destroy(&mutexForWorkList);
    pthread_cond_destroy(&condForWorkList);
    pthread_mutex_destroy(&mutexForFinishList);
    pthread_cond_destroy(&condForFinishList);

    //The workers will get work from the workList based on the wIndex
    // int wIndex;
    
    
    // for (wIndex = 0; wIndex < workListIndex; wIndex++){
    //     struct work currentWork = workList[wIndex];
    //     encoding(currentWork.charArray, currentWork.workSize, currentWork.index);
    // }
    struct encodedWork* final = finishList[workFinished - 1];
    unsigned char lastTwo[2];
    lastTwo[0] = (*final).last;
    lastTwo[1] = (*final).lastCount;
    write(1, lastTwo, 2);

}
