#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h> // gettimeofday


struct Buffer {
    int * SourceFdIdx;
    int * DestFdIdx;
    char ** FName;
    int size;
};

struct Buffer buf;

//semaphores
sem_t bufFull;
sem_t bufEmpty;
sem_t mutexP;
sem_t mutexC;

int totalCopy=0;
int consumerBufIdx=0;
int producerBufIdx=0;
int flagDone=0;

int producerThreadHelp(char * sourceFolder,char * destFolder);
void * producerThread(void * param);
void * consumerThread(void * param);
void destroyAll();
void handle_sigint(int signals);

int main(int argc, char *argv[]){

    if(argc==5){
        
        int ConsumerNums=0;
        struct timeval time1;
        struct timeval time2;

        gettimeofday(&time1, NULL);
        char * fileName[2];


        signal(SIGINT, handle_sigint);
        signal(SIGTERM, handle_sigint);
        signal(SIGQUIT, handle_sigint);

    
        ConsumerNums = atoi(argv[1]); //convert string to integer
        buf.size = atoi(argv[2]); //convert string to integer
        fileName[0] = argv[3]; //source_dir_path
        fileName[1] = argv[4]; //dest_dir_path

        buf.SourceFdIdx = malloc(buf.size*sizeof(int));
        buf.DestFdIdx = malloc(buf.size*sizeof(int));
        buf.FName = malloc(buf.size*sizeof(char*));


        
        int i=0;
        while(i<buf.size){
            buf.FName[i]="\0";
            i++;
        }

        pthread_t threadId;
        pthread_t ConsumerThreads[ConsumerNums*sizeof(pthread_t)];
    

        sem_init(&bufFull,0,0);
        sem_init(&bufEmpty,0,buf.size);
        sem_init(&mutexP,0,1);
        sem_init(&mutexC,0,1);

        pthread_create(&threadId, NULL, producerThread, fileName);
        


        i=0;
        while(i<ConsumerNums){
            pthread_create(&ConsumerThreads[i], NULL, consumerThread, NULL);
            i++;
        }

        pthread_join(threadId, NULL);
        i=0;
        while(i<ConsumerNums){
            pthread_join(ConsumerThreads[i],NULL);
            i++;
        }

        gettimeofday(&time2, NULL);


        int passedtime = (time2.tv_sec-time1.tv_sec)*pow(10,6)+ (time2.tv_usec-time1.tv_usec);

        printf("copied bytes : %d MB\n",totalCopy);
        
        printf("spent time : %d miliseconds\n",passedtime/1000);

        destroyAll(); 

        exit(0);
    }
    else{
        printf("there are missing or too much arguments, \nUsage: pCp [number of consumers] [buffer size] [source directory] [destination directory]");
        return 0;
    }
}



void * consumerThread(void * param){

    int remaining=0;

    for(;;){

        sem_wait(&mutexC); //lock semaphore mutexC 
        sem_getvalue(&bufFull,&remaining); //get the value of semaphore bufFull

        if(flagDone!=0 && remaining==0){ //if the value of semmaphore bufFull is 0 that means there is no item in the buffer

            sem_post(&mutexP); //unlock semaphore mutexP
            sem_post(&mutexC); //unlock semaphore mutexC     
            return 0;
        }

        sem_wait(&bufFull); //lock semaphore bufFull
        sem_getvalue(&bufFull,&remaining); //get the vlaue of semaphore bufFull
        if(flagDone!=0 && remaining==0){
            sem_post(&mutexC); //unlock semaphore mutexC
            return 0;
        }

        sem_wait(&mutexP);

        //critical section
        int sourceCopy = buf.SourceFdIdx[consumerBufIdx];
        int destCopy = buf.DestFdIdx[consumerBufIdx];
        char * fileNames = buf.FName[consumerBufIdx];
        buf.FName[consumerBufIdx]="\0";
        consumerBufIdx = (consumerBufIdx+1)%buf.size;
        sem_post(&mutexP);
        sem_post(&bufEmpty);
        sem_post(&mutexC);

        int totalSize=0;
        char buffer[1024];
        int r = read(sourceCopy,buffer, 1024);
        while (r > 0){
            totalSize+=r;
            write(destCopy,buffer,r);
            r = read(sourceCopy,buffer, 1024);

        }
        totalCopy += totalSize;
        printf("Copied file is: %s and its size is: %d \n",fileNames,totalSize);
        free(fileNames);
        close(destCopy);
        close(sourceCopy);

    }
}
void * producerThread(void * param){ //could take any number of parameters of unknown types
    char ** Folders = (char ** )param;
    char * sourceFolder = Folders[0];
    char * destFolder = Folders[1];

    producerThreadHelp(sourceFolder,destFolder);

    flagDone=1; 
    int sval;
    sem_getvalue(&bufFull,&sval);
    if(!(sval<0 || sval >0)){
        //unlock all 
        sem_post(&bufFull);
        sem_post(&mutexP);
        sem_post(&mutexC);
    }
    return 0;
}


int producerThreadHelp(char * sourceFolder,char * destFolder){
    
    struct stat st;
    if (lstat(destFolder, &st) == -1) {//Look at stat for checking if the directory exists,
        mkdir(destFolder, 0700); //And mkdir, to create a directory.
    }

    DIR * dr = opendir(sourceFolder);
    if(dr =='\0'){
        fprintf(stderr,"directory couldnt open, Usage : '%s' \n",sourceFolder);
        return 0;
    }

    struct dirent *de;

    
    while ((de=readdir(dr)) != '\0'){

        if(de -> d_name[0] != '.' || (de->d_name[1] != '.' && de->d_name[1] != 0)){

            char* pathNextSource = (char*)malloc(1024); 
            pathNextSource[0] = '\0';
            //concatanate
            pathNextSource = strcat(pathNextSource, sourceFolder);
            pathNextSource = strcat(pathNextSource, "/");
            pathNextSource = strcat(pathNextSource, de->d_name);

            char* pathNextDest = (char*)malloc(1024); 
            pathNextDest[0] = '\0';
            //concatanate
            pathNextDest = strcat(pathNextDest, destFolder);
            pathNextDest = strcat(pathNextDest, "/");
            pathNextDest = strcat(pathNextDest, de->d_name);
            
            if(de->d_type==DT_DIR){
                producerThreadHelp(pathNextSource,pathNextDest);
            }
            else if(de->d_type == DT_FIFO  || de->d_type == DT_REG  ){

                int fdirS = open(pathNextSource, O_RDONLY, S_IRWXU);
                if(fdirS==-1){
                    printf("Couldnt open the source file  %s\n",pathNextSource);
                }
                int fdirD = open(pathNextDest,O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
                if(fdirD==-1){
                    printf("Couldnt open the destination file  %s\n",pathNextDest);
                    close(fdirS);
                }

                sem_wait(&bufEmpty);
                sem_wait(&mutexP);

                buf.FName[producerBufIdx] = malloc(1024*sizeof(char));
                strcpy(buf.FName[producerBufIdx],de->d_name);
                buf.SourceFdIdx[producerBufIdx] = fdirS;
                buf.DestFdIdx[producerBufIdx] = fdirD;
                producerBufIdx = (producerBufIdx+1)%buf.size;

                sem_post(&mutexP);
                sem_post(&bufFull);

            }
            free(pathNextDest);
            free(pathNextSource);
        }
    }

    closedir(dr);
    return 0;
}



void handle_sigint(int signals){
    int i=0;
    while(i<buf.size){
        free(buf.FName[i]);
        i++;
    }
    destroyAll();
    printf("exit");

    exit(0);
}


void destroyAll(){
    sem_destroy(&mutexP);
    sem_destroy(&mutexC);
    sem_destroy(&bufFull);
    sem_destroy(&bufEmpty);
    free(buf.SourceFdIdx);
    free(buf.DestFdIdx);
    free(buf.FName);
}


