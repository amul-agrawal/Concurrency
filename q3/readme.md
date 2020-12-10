# Musical Mayhem Report
## Implementation
The simulation uses thread for each performer, coordinator and stage. All the necessary details for each of them are stored in their respective struct.

Following are the structs made:
```c
typedef struct Performer {
    int id;
    pthread_t tid;
    
    char* name;                         // name of performer
    char instrument;                    // instrument that he plays.
    int type;                           // type=0 => Musician, type=1 => Singer
    time_t performance_time;            // t'
    time_t wait_time;                   // wait_time, common for all.
    time_t arrival_time;                // arrival time of performer
    int state;                          // musicians have 7 states and singers have 5 states.
    int coperformer;                    // id of coperformer in case there are two performers on a stage.

    struct Stage* stage;                // pointer to the stage.

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
```
Apart from these I have also used two other global semaphores
```c
sem_t sem_receive_tshirt;
sem_t sem_dual_performance[MAX_DUAL_PERFORMANCES];
```
- `sem_receive_tshirts` is initialized to 0. Its semaphore is incremented by performers after their performance, which shows that they are ready for thsirt collection. Coordinators wait on this semaphore. Once a performer increments the count of this semaphore, the coordinator can decrement the count and award the performer his tshirt.
- `sem_dual_performance[MAX_DUAL_PERFORMANCES]` are also all initialized to zero. The singers who join musicians in their performance wait on this, untill musicians performance gets over and they increment its count.

## Performer

1. First the performer thread waits for its arrival, and then changes the performer state to `WAITING_TO_PERFORM`. The thread then conditionally waits for a fixed period of time for stage thread to make space for the performer. If the performer gets a chance to perform on the stage, it breaks the wait and starts performing, else leaves without performing.
    ```c
    // START AND WAIT.
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
    ```
2. The Performance:
    - if the performer is a singer and is performing with a musician, we wait on a semaphore corresponding to its id.
    - if the performer is a singer and is performing alone. we sleep for it's performance time.
    - if the performer is a musician and is performing solo, we sleep for it's performance time.
    - if the performer is a musician and is performing a duo, we sleep for it's performance time and then sleep for 2 extra seconds and then signal the coperformer(singer) and continue.
    ```c
    // PERFORMANCE
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
    ```
3. Waiting for tshirt.
    ```c
    sem_post(&(this->performing));
    this->stage = NULL;
    // signal to coordinators that my performance is done and I am ready to collect tshirt.
    sem_post(&(sem_receive_tshirt));
    if(num_coordinators == 0) {
        printf(ANSI_BRIGHT_BLUE "No Coordinators to distribute tshirts, hence returning." ANSI_DEFAULT);
        return NULL;
    }
    this->state = WAITING_FOR_TSHIRT;
    printf(ANSI_BRIGHT_BLUE "Waiting for Tshirt\n" ANSI_DEFAULT);
    // waiting for tshirt.
    sem_wait(&(this->receiving_tshirt));
    this->state = EXITED;
    printf(ANSI_RED "%s collected Tshirt and returned\n" ANSI_DEFAULT, this->name);
    ```
## Stage
4. If the stage is empty. we choose a random performer, check if he is ready to perform and fits the stage type. If yes we continue. Then we check if the performer is a musician or a singer. 
    - Singer => We signal the singer that he can perform on this stage, change the state of stage and wait for the performance of singer to get over. After this the state of stage again changes to empty and we continue.
    - Musician => We signal the musician that he can perform on this stage, change the state of stage to `STAGE_WITH_MUSICIAN`.
    ```c
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
    }
    ```
5. If the stage contains a musician. First we use `sem_trywait()` to check if the performance of musician is over or not. If it's over we change the state of stage to EMPTY and continue. If not we look for a singer who can join this performing musician. If we successfully find a singer who can join the musician, we change the state of stage to `STAGE_WITH_BOTH` and wait for both the performers to finish their performance.
    ```c
    else if(this->state == STAGE_WITH_MUSICIAN) {
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
    ```
## Coordinators
6. The coordinators first wait on `sem_receive_tshirt` semaphore. After a performer increments it's count, coordinator moves forward. The coordinator then picks a performer and spends 2 secs to look for tshirt and award it to the performer.
    ```c
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
    ```