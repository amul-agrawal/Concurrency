# Back to College Report
## Implementation
The simulation uses thread for each Company, Zone and Student. All the necessary details for students, zones and companies are stored in their struct. 

The structs are as follows: 
```c
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
    struct Student* students[BUFFER];
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
```
Apart from this I have also used one more global variable called `waiting_students` and a mutex `global_lock` to protect this variable. This variable denotes the total number of active student threads at any moment.

1. In the `company_proceed()` function the company manufactures certain predefined number of batches of vaccines and waits for zones to use all those batches, so that it can resume new production. The wait happens in other function called batches_ready(). I have used busy waiting in this question to implement wait.
    ```c
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
            // batches prepared, move to batches_ready() and wait for zones to take these batches.
            batches_ready(this_company);
            if(waiting_students == 0) return NULL; // EXIT IF ALL STUDENTS HAVE UNDERGONE VACCINATION.
        }
    }
    ```
    ```c
    void batches_ready(Company* this_company) {
        // BUSY WAIT FOR BATCH DISTRIBUTION.
        while (true) {      
            if (this_company->over_batches >= this_company->max_batches) {
                break;
            }
        }
        this_company->left_batches = 0;
        if(waiting_students == 0) return ;
        printf(ANSI_GREEN "All the vaccines prepared by Pharmaceutical Company %d are emptied. Resuming production now\n" 
            ANSI_DEFAULT, this_company->id); sleep(2);
    }
    ```
2. In the `zone_proceed()` function the first while loop randomly picks a company, checks if it has vaccine batches left, if yes it collects a batch from that company. This check should be done inside a mutex lock so that we can avoid multiple threads from occupying critical section together. 
    ```c
   while(true) {
       // pick a random company.
        int i = rand() % this_zone->num_companies;
        Company* company = this_zone->companies[i];
        pthread_mutex_lock(&(company->lock));
        // check if it has batches left
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
    ```
3. After a zone gets a batch, it begins vaccination in different phases.
4. In each phase we need to check the maximum number of slots that the zone can make available. `up` denotes the maximum number of slots that the zone can arrange in this phase.
    
5. Then the zone arranges some random number of slots for vaccination from the range `[1,up]`, after this the status of zone moves from `ZONE_READY` to `ZONE_ARRANGING`. The zone now is ready to serve students. It does so by waiting for students to come and get vaccinated in `ready_to_serve()` function.
    ```c
    // EACH PHASE.
    this_zone->state = ZONE_ARRANGING;
    this_zone->over_slots = 0;
    // up is upper_bound for slots in a vaccination phase.
    int up = min(8,min(waiting_students,this_zone->left_vaccines));
    if (up == 0) continue;
    this_zone->left_slots = this_zone->total_slots = 1 + rand()%up;
    this_zone->left_vaccines -= this_zone->total_slots;
    this_zone->state = ZONE_WORKING;
    printf(ANSI_MAGENTA "Vaccination Zone %d is ready to vaccinate with %d slots\n" ANSI_DEFAULT, this_zone->id, this_zone->total_slots);
    ready_to_serve_students(this_zone);
    ```
    ```c
    void ready_to_serve_students(Zone* this_zone) {
        while (true) {
            if (this_zone->over_slots >= this_zone->total_slots) {
                break;
            } 
        }
        this_zone->left_slots = 0;
        this_zone->state = ZONE_ARRANGING;
    }
    ```
6. The student thread first waits for certain random amount of time, which denotes the wait time for student arrival. After the wait, student arrives and increments the count of `waiting_students`.
7. Now after arrival of student, it picks a slot from a random zone. This slot picking is implemented in `slot_pick()` function.
    ```c
    void slot_pick(Student* this_student) {
        for(int i=0; i<1 ;i++) {
            // choosing a random zone.
            int choice = rand() % this_student->num_zones;
            Zone* zone = this_student->zones[choice];

            pthread_mutex_lock(&(zone->lock));

            // if the zone not working, continue to pick some other zone.
            if (zone->state != ZONE_WORKING) {
                pthread_mutex_unlock(&(zone->lock));
                i--;
                continue;
            } else {
                // found a working zone, picking this zone.
                int pos = zone->total_slots - zone->left_slots-1;
                zone->students[pos] = this_student;
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
    ```
8. After the student picks a slot, it waits for 2 secs, which is vaccination time. After this the student gets vaccinated and now moves to testing. The probability of a vaccine working is already given input, so success of testing results depends on this probability. The testing part has been implemented in the following way.
    ```c
    int testing(Student* this_student) {
        long double x = this_student->assigned_zone->assigned_company->strength;
        x = 10000*x;
        int y = (int)x;
        int num = rand()%10000; // [0,9999]
        if(num < y) return 1;
        else return 0;
    }
    ```
9. If student is tested positive for antibodies, his thread returns after decrementing the count of `waiting_students`, else he moves for one more round of vaccination. He is allowed at most 3 rounds of vaccination.
