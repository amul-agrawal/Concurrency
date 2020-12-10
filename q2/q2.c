#define _POSIX_C_SOURCE 199309L  //required for clock
#include <math.h>
#include <wait.h>
#include <stdbool.h>
#include <limits.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/ipc.h>

// COLOURS
#define ANSI_RED "\033[1;31m"
#define ANSI_CYAN "\033[1;36m"
#define ANSI_YELLOW "\033[1;33m"
#define ANSI_MAGENTA "\033[1;35m"
#define ANSI_GREEN "\033[1;32m"
#define ANSI_DEFAULT "\033[0m"
#define ANSI_CLEAR "\033[2J\033[1;1H"

#define BUFFER 4096
#define ZONE_READY 0
#define ZONE_ARRANGING 1
#define ZONE_WORKING 2
#define MAX_TURNS_PER_STUDENT 3

int waiting_students=0;
int num_companies, num_zones, num_students;
pthread_mutex_t global_lock;


typedef struct Company {
    int id;
    pthread_t tid;
    pthread_mutex_t lock;
    int prep_time;
    int max_batches;
    int left_batches;
    int batch_size;
    int over_batches;
    long double strength;

} Company;

typedef struct Zone {
    int id;
    pthread_t tid;
    Company** companies;
    int num_companies;
    Company* assigned_company;
    int left_vaccines;
    int state;
    int total_slots;
    int left_slots;
    int over_slots;
    pthread_mutex_t lock;
} Zone;

typedef struct Student {
    int id;
    pthread_t tid;
    time_t arrival_time;
    Zone* assigned_zone;
    struct Zone** zones;
    int num_zones;
} Student;


void* company_proceed(void* args);
void batches_ready(Company* this_company);
void* zone_proceed(void* args);
void ready_to_serve_students(Zone* this_zone);
int testing(Student* this_student);
void* student_proceed(void* args);
void slot_pick(Student* this_student);

int testing(Student* this_student) {
    long double x = this_student->assigned_zone->assigned_company->strength;
    x = 10000*x;
    int y = (int)x;
    int num = rand()%10000; // [0,9999]
    if(num < y) return 1;
    else return 0;
}

int min(int a, int b) {
    if (a > b) return b;
    else return a;
}

// ----------------------------------------------------------------------------------------------------------------------------------------------------
void* company_proceed(void* args) {
    Company* this_company = (Company*)args;

    while (true) {
        this_company->left_batches = 0;
        this_company->over_batches = 0;

        printf(ANSI_GREEN "Pharmaceutical Company %d is preparing %d batches of vaccines which have success probability %Lf\n" ANSI_DEFAULT,
            this_company->id, this_company->max_batches, this_company->strength); 

        sleep(this_company->prep_time);

        printf(ANSI_GREEN "Pharmaceutical Company %d has prepared %d batches of vaccines which have success probability %Lf\n" ANSI_DEFAULT,
            this_company->id, this_company->max_batches, this_company->strength); 

        this_company->left_batches = this_company->max_batches;

        batches_ready(this_company);
        if(waiting_students == 0) return NULL; // EXIT IF ALL STUDENTS HAVE UNDERGONE VACCINATION.
      
        printf(ANSI_GREEN "All the vaccines prepared by Pharmaceutical Company %d are emptied. Resuming production now\n" 
          ANSI_DEFAULT, this_company->id); sleep(2);
    }
}

void batches_ready(Company* this_company) {
    // BUSY WAIT FOR BATCH DISTRIBUTION.
    while (true) {      
        if (this_company->over_batches >= this_company->max_batches) {
            break;
        }
    }
    return;
}
// ----------------------------------------------------------------------------------------------------------------------------------------------------


// ----------------------------------------------------------------------------------------------------------------------------------------------------
void* zone_proceed(void* args) {
    Zone* this_zone = (Zone*)args;

    while(true) {
        // THE ZONE HAS ENTERED READY STATE.
        this_zone->state = ZONE_READY;

        // LOOKING FOR A BATCH FROM COMPANY.
        while(true) {
            int i = rand() % this_zone->num_companies;
            Company* company = this_zone->companies[i];
            pthread_mutex_lock(&(company->lock));

            if (company->left_batches > 0) {
                this_zone->assigned_company = company;
                this_zone->left_vaccines = company->batch_size;
                printf(ANSI_GREEN "Pharmaceutical Company %d is delivering a vaccine batch to Vaccination"
                    " Zone %d which has success probability %Lf\n" ANSI_DEFAULT, company->id, this_zone->id, this_zone->assigned_company->strength);
                company->left_batches--;
                pthread_mutex_unlock(&(company->lock));
                break;
            }
            pthread_mutex_unlock(&(company->lock));
        }

        printf(ANSI_GREEN "Pharmaceutical Company %d has delivered vaccines to Vaccination zone %d, resuming vaccinations now\n" 
            ANSI_DEFAULT, this_zone->assigned_company->id, this_zone->id);

        printf(ANSI_MAGENTA "Vaccination Zone %d entering Vaccination Phase\n" ANSI_DEFAULT, this_zone->id); sleep(2);
        while (true) {
            // if vaccines are over, stop vaccination phase.
            if (this_zone->left_vaccines <= 0 ) { 
                this_zone->left_vaccines = 0;
                break;
            }

            this_zone->state = ZONE_ARRANGING;
            this_zone->over_slots = 0;

            // up is upper_bound for slots in a vaccination phase.
            int up = min(8,min(waiting_students,this_zone->left_vaccines));

            if (up <= 0) continue;

            this_zone->left_slots = this_zone->total_slots = 1 + rand()%up;

            this_zone->left_vaccines -= this_zone->total_slots;

            printf(ANSI_MAGENTA "Vaccination Zone %d is ready to vaccinate with %d slots\n" ANSI_DEFAULT, 
                this_zone->id, this_zone->total_slots);

            this_zone->state = ZONE_WORKING;

            ready_to_serve_students(this_zone);
            // printf("id:%d back from ready_to_serve\n",this_zone->id);

        }
        printf(ANSI_MAGENTA "Vaccination Zone %d has run out of vaccines\n" ANSI_DEFAULT, this_zone->id);
        this_zone->assigned_company->over_batches++;
    }
}

void ready_to_serve_students(Zone* this_zone) {
    while (true) {
        if (this_zone->over_slots >= this_zone->total_slots) {
            // printf("id:%d over_slots:%d total_slots:%d\n", this_zone->id, this_zone->over_slots, this_zone->total_slots);
            break;
        } 
    }
    this_zone->left_slots = 0;
    this_zone->state = ZONE_ARRANGING;
}
// ----------------------------------------------------------------------------------------------------------------------------------------------------


// ----------------------------------------------------------------------------------------------------------------------------------------------------

void* student_proceed(void* args) {
    Student* this_student = (Student*)args;
    
    this_student->arrival_time = rand() % 20;
    sleep(this_student->arrival_time);

    pthread_mutex_lock(&(global_lock));
    waiting_students++;
    pthread_mutex_unlock(&(global_lock));
    int turns = MAX_TURNS_PER_STUDENT;
    while(turns--) {
        printf(ANSI_RED "Student %d has arrived for round %d vaccination\n" ANSI_DEFAULT, this_student->id, 3-turns); sleep(2);
        printf(ANSI_RED "Student %d is waiting to be allocated a slot on a Vaccination Zone\n" ANSI_DEFAULT, this_student->id); 

        // taking slot
        slot_pick(this_student);
        sleep(2);
        printf(ANSI_YELLOW "Student %d on Vaccination Zone %d has been vaccinated which has success probability %Lf\n" ANSI_DEFAULT, this_student->id,
         this_student->assigned_zone->id, this_student->assigned_zone->assigned_company->strength);

        // vaccinating
        int pass = testing(this_student);
        if(pass) {
            printf("\033[38;5;51mStudent %d has tested positive for antibodies\n\033[m", this_student->id);
            pthread_mutex_lock(&(global_lock));
            waiting_students++;
            pthread_mutex_unlock(&(global_lock));
            this_student->assigned_zone->over_slots++;
            break;
        } else {
            printf("\033[38;5;51mStudent %d has tested negative for antibodies\n\033[m", this_student->id);
        }
        pthread_mutex_lock(&(global_lock));
        waiting_students++;
        pthread_mutex_unlock(&(global_lock));
        this_student->assigned_zone->over_slots++;
    }
    pthread_mutex_lock(&(global_lock));
    waiting_students--;
    pthread_mutex_unlock(&(global_lock));
    return NULL;
}

void slot_pick(Student* this_student) {
    for(int i=0; i<1 ;i++) {
        // choosing a random zone.
        int choice = rand() % this_student->num_zones;
        Zone* zone = this_student->zones[choice];

        pthread_mutex_lock(&(zone->lock));

        // if the zone not working, continue to pick some other zone.
        if (zone->state != ZONE_WORKING || zone->left_slots<=0) {
            pthread_mutex_unlock(&(zone->lock));
            i--;
            continue;
        } else {
            // found a working zone, picking this zone.
            int pos = zone->total_slots - zone->left_slots-1;
            zone->left_slots--;

            this_student->assigned_zone = zone;
            pthread_mutex_lock(&(global_lock));
            waiting_students--;
            pthread_mutex_unlock(&(global_lock));

            printf(ANSI_YELLOW "Student %d assigned a slot on the Vaccination Zone %d and waiting to be vaccinated\n" ANSI_DEFAULT, this_student->id, zone->id);

            pthread_mutex_unlock(&(zone->lock));
        }
    }
}
// ----------------------------------------------------------------------------------------------------------------------------------------------------


signed main() {
    srand(time(0));

    printf("Enter number of companies, zones and students: ");
    scanf("%d %d %d", &num_companies, &num_zones, &num_students);

    printf("Enter Probabilities\n");

    Company** companies = (Company**)malloc(sizeof(Company*) * num_companies);
    for (int i = 0; i < num_companies; i++) {
        companies[i] = (Company*)malloc(sizeof(Company));
        scanf("%Lf",&companies[i]->strength);
    }

    if ( num_zones == 0 || num_companies == 0 || num_students == 0) {
        printf("Simulation Over.\n");
        return 0;
    }

    pthread_mutex_init(&(global_lock), NULL);
    printf("Running....\n");
    for (int i = 0; i < num_companies; i++) {
        companies[i]->id = i;
        companies[i]->batch_size = 10 + rand() % 11;
        companies[i]->prep_time = 2 + rand() % 4;
        companies[i]->max_batches = 1 + rand() % 5;
        pthread_mutex_init(&(companies[i]->lock), NULL);
        pthread_create(&(companies[i]->tid), NULL, company_proceed, (void*)companies[i]);
    }

    Zone** zones = (Zone**)malloc(sizeof(Zone*) * num_zones);
    for (int i = 0; i < num_zones; i++) {
        zones[i] = (Zone*)malloc(sizeof(Zone));
        zones[i]->id = i;
        zones[i]->companies = companies;
        zones[i]->state = ZONE_READY;
        zones[i]->num_companies = num_companies;
        zones[i]->assigned_company = NULL;
        pthread_mutex_init(&(zones[i]->lock), NULL);
        pthread_create(&(zones[i]->tid), NULL, zone_proceed, (void*)zones[i]);
    }

    Student** students = (Student**)malloc(sizeof(Student*) * num_students);
    for (int i = 0; i < num_students; i++) {
        students[i] = (Student*)malloc(sizeof(Student));
        students[i]->id = i;
        students[i]->num_zones = num_zones;
        students[i]->zones = zones;
        students[i]->assigned_zone = NULL;
        pthread_create(&(students[i]->tid), NULL, student_proceed, (void*)students[i]);
    }

    for (int i = 0; i < num_students; i++) {    
        pthread_join(students[i]->tid, NULL);
    }

    printf("Simulation Over.\n");

    int i=0,j=0;
    while(i<num_zones) {
        pthread_mutex_destroy(&(zones[i]->lock));
        i++;
    }
    while(j<num_companies) {
        pthread_mutex_destroy(&(companies[j]->lock));
        j++;
    }
    return 0;
}


