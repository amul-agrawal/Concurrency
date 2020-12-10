#define _POSIX_C_SOURCE 199309L  //required for clock
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <wait.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <string.h>

// COLOURS
#define ANSI_RED "\033[1;31m"
#define ANSI_GREEN "\033[1;32m"
#define ANSI_YELLOW "\033[1;33m"
#define ANSI_MAGENTA "\033[1;35m"
#define ANSI_CYAN "\033[1;36m"
#define ANSI_BLUE "\e[1;34m"
#define ANSI_BRIGHT_BLUE "\e[1;94m"
#define ANSI_RED_BOLD "\033[1;31m"
#define ANSI_GREEN_BOLD "\033[1;32m"
#define ANSI_YELLOW_BOLD "\033[1;33m"
#define ANSI_CYAN_BOLD "\033[1;36m"
#define ANSI_DEFAULT "\033[0m"
#define ANSI_CLEAR "\033[2J\033[1;1H"

#define NAME_BUFFER 1024
#define MAX_DUAL_PERFORMANCES 256

// STATES OF STAGE
#define ACOUSTIC 0
#define ELECTRIC 1
#define STAGE_EMPTY 0
#define STAGE_WITH_MUSICIAN 1
#define STAGE_WITH_SINGER 2
#define STAGE_WITH_BOTH 3


// STATES OF PERFORMER
#define MUSICIAN 0
#define SINGER 1

#define NOT_YET_ARRIVED 0
#define WAITING_TO_PERFORM 1
#define PERFORMING_SOLO 2
#define PERFORMING_WITH_MUSICIAN 3
#define PERFORMING_WITH_SINGER 4
#define WAITING_FOR_TSHIRT 5
#define COLLECTING_TSHIRT 6
#define EXITED 7

// STATES OF COORDINATORS
#define  COORDINATOR_SETUP 0

sem_t sem_receive_tshirt;
sem_t sem_dual_performance[MAX_DUAL_PERFORMANCES];
int num_coordinators;

typedef struct Performer {
    int id;
    pthread_t tid;
    
    char* name;
    char instrument;
    int type;                           // type=0 => Musician, type=1 => Singer
    time_t performance_time;            // t'
    time_t wait_time;                   // wait_time, common for all.
    time_t arrival_time;                // arrival time of performer
    int state;                          // musicians have 7 states and singers have 5 states.
    int coperformer;

    struct Stage* stage;

    pthread_mutex_t lock;               // protects the critical data of the performer
    pthread_cond_t cv_stage;            // conditional variables to use along with lock

    sem_t performing;                   // to block stage while performer is performing
    sem_t receiving_tshirt;            // to block thread while receiving_tshirt

} Performer;

typedef struct Stage {
    int id;
    pthread_t tid;

    int type;                           // type=0 => accoustic stage, type=1 => electric stage.
    int state;                          // stage will be in different states.
    char* stage_name;
    struct Performer* performer[2];      // At Max 2 performers are allowed.

    struct Performer** performers; 
    int num_performers;

} Stage;

typedef struct Coordinator {
    int id;
    pthread_t tid;

    int state;

    struct Performer* performer;

    struct Performer** performers;
    int num_performers;
} Coordinator;

void stage_setup(Stage* stage, int idx, Performer** performers, int num_performers,int stage_type);
void performer_setup(Performer* performer, int idx, char* name, char instrument, time_t t, time_t wait_time, time_t t1, time_t t2);
void* stage_exec(void* args);
void* performer_exec(void* args);
void* coordinator_exec(void* args);

void stage_setup(Stage* stage, int idx, Performer** performers, int num_performers,int stage_type) {
    stage->id = idx;
    for (int i=0; i<2; i++) {
        stage->performer[i] = NULL;
    }
    stage->type = stage_type;
    stage->state = STAGE_EMPTY;
    stage->num_performers = num_performers;
    stage->performers = performers;
    stage->stage_name = (char *)malloc(NAME_BUFFER*sizeof(char));
    if(stage_type == ACOUSTIC) {
        strcpy(stage->stage_name, "acoustic");
    } else {
        strcpy(stage->stage_name, "electric");
    }
}


void performer_setup(Performer* performer, int idx, char* name, char instrument, time_t t, time_t wait_time, time_t t1, time_t t2) {
    performer->id = idx;
    performer->instrument = instrument;
    performer->name  = (char *)malloc(NAME_BUFFER*sizeof(char));
    strcpy(performer->name, name);
    performer->arrival_time = t;
    performer->wait_time = wait_time;
    performer->state = NOT_YET_ARRIVED;
    if(instrument != 's') {
        performer->type = MUSICIAN;
    } else {
        performer->type = SINGER;
    }
    performer->stage = NULL;
    performer->performance_time = t1 + rand()%(t2-t1+1);
    sem_init(&(sem_dual_performance[idx]), 0, 0);
    sem_init(&(performer->performing), 0, 0);
    sem_init(&(performer->receiving_tshirt), 0, 0);

    pthread_mutex_init(&(performer->lock), NULL);
    pthread_cond_init(&(performer->cv_stage), NULL);

}


void* performer_exec(void* args) {
    Performer* this = (Performer*)args;
    // Time Before Arrival
    sleep(this->arrival_time);
    // Arrived and now waiting to perform
    pthread_mutex_lock(&(this->lock));
    this->state = WAITING_TO_PERFORM;
    // PRINT STATEMENT 1
    printf(ANSI_GREEN "%s %c arrived.\n" ANSI_DEFAULT, this->name, this->instrument);
    
    struct timespec clock;
    clock_gettime(CLOCK_REALTIME, &clock);
    clock.tv_sec = clock.tv_sec + this->wait_time;

    while (true) { 
        if (this->state == WAITING_TO_PERFORM) {
            if (pthread_cond_timedwait(&(this->cv_stage), &(this->lock), &clock) == ETIMEDOUT){
                this->state = EXITED;
                printf(ANSI_RED_BOLD "%s left because of impatience\n" ANSI_DEFAULT, this->name);
                pthread_mutex_unlock(&(this->lock));
                return NULL;
            }
        } else {
            break;
        }
    }
    pthread_mutex_unlock(&(this->lock));
    // performing
    if (this->type == SINGER) {
        // now the state could be solo or dual.
        if (this->state == PERFORMING_WITH_MUSICIAN) {
            // condition wait.
            sem_wait(&(sem_dual_performance[this->id]));
        } else {
            sleep(this->performance_time);
        }
    } else {
        // state is solo.
        sleep(this->performance_time);
        pthread_mutex_lock(&(this->lock));
        if (this->state == PERFORMING_WITH_SINGER) {
            pthread_mutex_unlock(&(this->lock));
            sleep(2);
            // send condition signal to the singer.
            sem_post(&(sem_dual_performance[this->coperformer]));
        } else {
            pthread_mutex_unlock(&(this->lock));
        }
    }
    
    // posting after my performance is done.
    if(this->type == MUSICIAN) {
        printf(ANSI_YELLOW "%s performance at %s stage %d finished\n" ANSI_DEFAULT, this->name, this->stage->stage_name, this->stage->id);
    } else {
        printf(ANSI_YELLOW "%s singing at %s stage %d  finished\n" ANSI_DEFAULT, this->name, this->stage->stage_name, this->stage->id);
    }
    // signal that my performance is done.
    sem_post(&(this->performing));
    this->stage = NULL;
        // if (this->type == SINGER) {
        //     this->state = EXITED;
        //     printf(ANSI_RED "%s returned\n" ANSI_DEFAULT, this->name);
        //     return NULL;
        // } 
    // waiting for tshirt
    sem_post(&(sem_receive_tshirt));
    if(num_coordinators == 0) {
        printf(ANSI_BRIGHT_BLUE "No Coordinators to distribute tshirts, hence returning." ANSI_DEFAULT);
        return NULL;
    }
    this->state = WAITING_FOR_TSHIRT;
    printf(ANSI_BRIGHT_BLUE "%s Waiting for Tshirt\n" ANSI_DEFAULT, this->name);
    sem_wait(&(this->receiving_tshirt));
    this->state = EXITED;
    printf(ANSI_RED "%s collected Tshirt and returned\n" ANSI_DEFAULT, this->name);
    return NULL;
}

void* stage_exec(void* args) {
    Stage* this = (Stage*)args;
    while(true) {
        if(this->state == STAGE_EMPTY) {
            // picking a random performer
            int performer_id = rand()%this->num_performers;
            Performer* performer = this->performers[performer_id];
            // performer did not match the stage.
            if((this->type == ACOUSTIC && performer->instrument == 'b') || (this->type == ELECTRIC && performer->instrument == 'v')) {
                continue;
            }
            pthread_mutex_lock(&(performer->lock));
            // performer not waiting
            if(performer->state != WAITING_TO_PERFORM) {
                pthread_cond_signal(&(performer->cv_stage));
                pthread_mutex_unlock(&(performer->lock));
                continue;
            }
            // The stage can pick this performer now.
            switch(performer->type) {
                case SINGER: 
                    // occupies the stage and doesn't leave it untill performance is done.
                    if(pthread_cond_signal(&(performer->cv_stage)) == 0) {

                        this->state = STAGE_WITH_SINGER;
                        this->performer[0] = performer;

                        performer->state = PERFORMING_SOLO;
                        performer->stage = this;
            
                        pthread_mutex_unlock(&(performer->lock));

                        // PRINT STATEMENT 2
                        printf(ANSI_MAGENTA "%s singing at %s stage %d for %ld sec\n" ANSI_DEFAULT, 
                            performer->name, this->stage_name, this->id, performer->performance_time);
                        // wait for performer to perform
                        sem_wait(&(performer->performing));

                        this->state = STAGE_EMPTY;
                        this->performer[0] = NULL;
                        continue;
                    } else {
                        pthread_mutex_unlock(&(performer->lock));
                        continue;
                    }
                    break;
                case MUSICIAN:
                    if(pthread_cond_signal(&(performer->cv_stage)) == 0) {
                        this->state = STAGE_WITH_MUSICIAN;
                        this->performer[0] = performer;

                        performer->state = PERFORMING_SOLO;
                        performer->stage = this;

                        pthread_mutex_unlock(&(performer->lock));
                        printf(ANSI_MAGENTA "%s performing %c at %s stage %d for %ld sec\n" ANSI_DEFAULT, 
                            performer->name, performer->instrument, this->stage_name, this->id, performer->performance_time);
                        continue;
                    } else {
                        pthread_mutex_unlock(&(performer->lock));
                        continue;
                    }
                    break;
                default: 
                    pthread_mutex_unlock(&(performer->lock));
                    break;
            }
        } else if(this->state == STAGE_WITH_MUSICIAN) {
            // check if the first musician is done?
            if (sem_trywait(&(this->performer[0]->performing)) == 0) {
                this->performer[0] = NULL;  
                this->state = STAGE_EMPTY;
                continue;
            }
            // not done. look for a free singer to accompany musician.
            int performer_id=rand()%this->num_performers;
            Performer* performer=this->performers[performer_id];

            if(pthread_mutex_lock(&(performer->lock))!=0) {
                perror("mutex lock error\n");
            }

            if(performer->type != SINGER || performer->state != WAITING_TO_PERFORM) {
                if(performer->type  == SINGER) {
                    pthread_cond_signal(&(performer->cv_stage));
                }
                pthread_mutex_unlock(&(performer->lock));
                continue;
            }   
            // found a SINGER WAITING.
            if(pthread_cond_signal(&(performer->cv_stage)) == 0) {

                this->state = STAGE_WITH_BOTH;
                this->performer[1] = performer;

                performer->state = PERFORMING_WITH_MUSICIAN;
                performer->stage = this;

                this->performer[0]->state = PERFORMING_WITH_SINGER;

                performer->coperformer = this->performer[0]->id;
                this->performer[0]->coperformer = performer->id;

                pthread_mutex_unlock(&(performer->lock));
                printf(ANSI_MAGENTA "%s joined %s's performance at stage %d, performance extended by 2 secs\n" ANSI_DEFAULT,
                     this->performer[1]->name, this->performer[0]->name, this->id);

                // wait for performers to perform
                for (int i=0;i<2;i++) {
                    sem_wait(&(this->performer[i]->performing));
                }

                this->state = STAGE_EMPTY;
                this->performer[0] = NULL;
                continue;
            } else {
                pthread_mutex_unlock(&(performer->lock));
                continue;
            }
        }
    }
}

void* coordinator_exec(void* args) {
    Coordinator* this = (Coordinator*)args;
    while(true) {
        sem_wait(&sem_receive_tshirt);
        // printf("After sem wait\n");
        for (int j=1 ; j <= this->num_performers; j++) {

            int performer_id = j-1;
            pthread_mutex_lock(&(this->performers[performer_id]->lock));
            if (this->performers[performer_id]->state != WAITING_FOR_TSHIRT) {
                pthread_mutex_unlock(&(this->performers[performer_id]->lock));
            } else {
                this->performer = this->performers[performer_id];
                this->performer->state = COLLECTING_TSHIRT;
                printf(ANSI_BLUE "%s collecting tshirt\n" ANSI_DEFAULT, this->performer->name);
                pthread_mutex_unlock(&(this->performer->lock));
                break;
            }
        }

        if (this->performer == NULL) {
            continue;
        }
    
        sleep(2);
        sem_post(&(this->performer->receiving_tshirt));
        this->performer = NULL;
    }
}


signed main() {
    srand(time(0));

    int num_a_stages, num_e_stages, num_performers, t1, t2, wait_time;
    printf("Enter Number of Performers: "); scanf("%d", &num_performers);
    printf("Enter Number of Accoustic Stages: "); scanf("%d", &num_a_stages);
    printf("Enter Number of Electric Stages: "); scanf("%d", &num_e_stages);
    printf("Enter Number of Coordinators: "); scanf("%d", &num_coordinators);
    printf("Enter t1 and t2: "); scanf("%d %d", &t1, &t2);
    printf("Enter Wait time: "); scanf("%d", &wait_time);   

    sem_init(&sem_receive_tshirt, 0, 0);
    
    Coordinator** coordinators = (Coordinator**)malloc(num_coordinators*sizeof(Coordinator*));
    Performer** performers = (Performer**)malloc(num_performers*sizeof(Performer*));
    Stage** stages = (Stage**)malloc((num_a_stages + num_e_stages)*sizeof(Stage*));

    for (int i = 0; i < num_performers; i++) {
        char* name = (char*)malloc(NAME_BUFFER*sizeof(char));
        char instrument;
        time_t arrival_time;
        printf("Enter Name, Instrument, Arrival Time for Performer %d: ",  i); 
        scanf("%s %c %ld", name, &instrument, &arrival_time);
        performers[i] = (Performer*)malloc(sizeof(Performer));
        performer_setup(performers[i], i, name, instrument, arrival_time, wait_time, t1, t2);
        free(name);
    }
    for (int i = 0; i < num_a_stages; i++) {
        stages[i] = (Stage*)malloc(sizeof(Stage));
        stage_setup(stages[i], i, performers, num_performers, ACOUSTIC);
    }
    for (int i = num_a_stages; i < (num_a_stages + num_e_stages); i++) {
        stages[i] = (Stage*)malloc(sizeof(Stage));
        stage_setup(stages[i], i, performers, num_performers, ELECTRIC);
    }
    for (int i = 0; i < num_coordinators; i++) {
        coordinators[i] = (Coordinator*)malloc(sizeof(Coordinator));
        coordinators[i]->performers = performers;
        coordinators[i]->num_performers = num_performers;
        coordinators[i]->id = i;
        coordinators[i]->performer = NULL;
        coordinators[i]->state = COORDINATOR_SETUP;
    }
    
    printf("\nRunning....\n");
    if(num_performers == 0) {
        printf("finished\n");
        return 0;
    }

    for (int i = 0; i < num_performers; i++) {
        pthread_create(&(performers[i]->tid), NULL, performer_exec, (void*)performers[i]);
    }
    for (int i = 0; i < num_a_stages; i++) {
        pthread_create(&(stages[i]->tid), NULL, stage_exec, (void*)stages[i]);
    }
    for (int i = num_a_stages; i < (num_a_stages + num_e_stages); i++) {
        pthread_create(&(stages[i]->tid), NULL, stage_exec, (void*)stages[i]);
    }
    for (int i = 0; i < num_coordinators; i++) {
        pthread_create(&(coordinators[i]->tid), NULL, coordinator_exec, coordinators[i]);
    }
    for (int i = 0; i < num_performers; i++) {
        pthread_join(performers[i]->tid, NULL);
    }

    printf("\nFinished.\n");

    for (int i = 0; i < num_performers; i++) {
        sem_destroy(&(sem_dual_performance[i]));
        pthread_mutex_destroy(&(performers[i]->lock));
    }
    sem_destroy(&sem_receive_tshirt); 
    return 0;
}