#define _POSIX_C_SOURCE 199309L //required for clock
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>        
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wait.h>
#include <limits.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>

#define IPC_RESULT_ERROR (-1)
int shared_block_id;

void get_shared_block(int size) {
    key_t key = IPC_PRIVATE;
    shared_block_id = shmget(key, size, 0644 | IPC_CREAT);
}

int *attach_memory_block(int size) {
    get_shared_block(size);
    int *result; // int because of integer array.

    if(shared_block_id == IPC_RESULT_ERROR) {
        perror("shmget() error");
        return NULL;
    }

    result = shmat(shared_block_id, NULL, 0);
    if(result == (int *)IPC_RESULT_ERROR) {
        perror("shmat() error");
        return NULL;
    }

    return result;
}

bool detach_memory_block(int *block) {
    return (shmdt(block) != IPC_RESULT_ERROR);
}

bool destroy_memory_block() {
    if(shared_block_id == IPC_RESULT_ERROR) {
        return NULL;
    }
    return (shmctl(shared_block_id, IPC_RMID, NULL) != IPC_RESULT_ERROR);
}

void swap(int* a, int* b)
{
    int t = *a;
    *a = *b;
    *b = t;
}

void selection_sort(int *arr,int low,int high)
{
    for (int i = low; i <= high; i++)
    {
        int min_idx = i;
        for(int j=i+1;j<=high;j++)
        {
            if(arr[j] < arr[min_idx]) 
            {
                min_idx = j;
            }
        }
        swap(&arr[i],&arr[min_idx]);
    }
}

void merge(int *arr,int low,int mid,int high)
{

    int n1 = mid - low + 1; 
    int n2 = high - mid; 
  
    // Create temp arrays  
    int L[n1], R[n2]; 
  
    // Copy data to temp arrays L[] and R[]  
    for(int i = 0; i < n1; i++) 
        L[i] = arr[low + i]; 
    for(int j = 0; j < n2; j++) 
        R[j] = arr[mid + 1 + j]; 

    // Merge the temp arrays back into arr[l..r] 
      
    // Initial index of first subarray 
    int i = 0;  
      
    // Initial index of second subarray 
    int j = 0;  
      
    // Initial index of merged subarray 
    int k = low; 
      
    while (i < n1 && j < n2) 
    { 
        if (L[i] <= R[j])  
        { 
            arr[k] = L[i]; 
            i++; 
        } 
        else 
        { 
            arr[k] = R[j]; 
            j++; 
        } 
        k++; 
    } 

    // Copy the remaining elements of 
    // L[], if there are any  
    while (i < n1)  
    { 
        arr[k] = L[i]; 
        i++; 
        k++; 
    } 

    // Copy the remaining elements of 
    // R[], if there are any  
    while (j < n2) 
    { 
        arr[k] = R[j]; 
        j++; 
        k++; 
    }
}

void normal_mergesort(int *arr, int low, int high)
{
    if(high-low+1 < 5)
    {
        selection_sort(arr,low,high);
    }
    else if(low<high)
    {
        int mid = low + (high-low)/2;
        normal_mergesort(arr, low, mid);
        normal_mergesort(arr, mid + 1, high);
        merge(arr,low,mid,high);
    }
}

void forked_mergesort(int *arr,int low,int high)
{
    if(high-low+1 < 5)
    {
        selection_sort(arr,low,high);
    }
    else
    {
        int mid = (low + high)/2;
        int pidL = fork();
        if(pidL == 0)
        {
            forked_mergesort(arr,low,mid);
            _exit(EXIT_SUCCESS);
        }
        else
        {
            int pidR = fork();
            if(pidR == 0)
            {
                forked_mergesort(arr,mid+1,high);
                _exit(EXIT_SUCCESS);
            }
            else
            {
                int status;
                waitpid(pidL, &status, 0);
                waitpid(pidR, &status, 0);
                merge(arr,low,mid,high);
            }
        }
    }
}

struct arg{
     int low;
     int high;
     int* arr;
};

void *threaded_mergesort(void* a)
{
    //note that we are passing a struct to the threads for simplicity.
    struct arg *args = (struct arg*) a;

    int low = args->low;
    int high = args->high;
    int *arr = args->arr;
    if(low>high) return NULL;

    if(high-low+1 < 5)
    {
        selection_sort(arr,low,high);
        return NULL;
    }

    int mid = low + (high-low)/2;

    //sort left half array
    struct arg a1;
    a1.low = low;
    a1.high = mid;
    a1.arr = arr;
    pthread_t tid1;
    pthread_create(&tid1, NULL, threaded_mergesort, &a1);

    //sort right half array
    struct arg a2;
    a2.low = mid+1;
    a2.high = high;
    a2.arr = arr;
    pthread_t tid2;
    pthread_create(&tid2, NULL, threaded_mergesort, &a2);

    //wait for the two halves to get sorted
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    merge(arr,low,mid,high);
}


void runSorts(long long int n)
{
    struct timespec ts;
    long double en, st;

    //getting shared memory
    int *arr = attach_memory_block(sizeof(int)*(n+2));
    if(arr == NULL)
    {
        printf("Error: Couldn't get shared memory block\n");
        return ;
    }
    for(int i=0;i<n;i++) scanf("%d", arr+i);

    int *brr = (int *)malloc((n+2)*sizeof(int));
    for(int i=0;i<n;i++) brr[i] = arr[i];

    printf("Running \033[38;5;51mforked_concurrent_mergesort\033[m for n = %lld\n", n);
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    st = ts.tv_nsec/(1e9)+ts.tv_sec;

    forked_mergesort(arr, 0, n-1);
    for(int i=0; i<n; i++){
        printf("%d ",arr[i]);
    }
    printf("\n");
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    en = ts.tv_nsec/(1e9)+ts.tv_sec;
    printf("\033[38;5;51mtime = %Lf\n\033[m", en - st);
    long double t1 = en-st;

    pthread_t tid;
    struct arg a;
    a.low = 0;
    a.high = n-1;
    a.arr = (int*)malloc((n+2)*sizeof(int));
    for (int i = 0; i < n; i++)
    {
        a.arr[i]=brr[i];
    }
    
    printf("Running \033[38;5;112mthreaded_concurrent_mergesort\033[m for n = %lld\n", n);
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    st = ts.tv_nsec/(1e9)+ts.tv_sec;

    pthread_create(&tid, NULL, threaded_mergesort, &a);
    pthread_join(tid, NULL);
    for(int i=0; i<n; i++){
        printf("%d ",a.arr[i]);
    }
    printf("\n");
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    en = ts.tv_nsec/(1e9)+ts.tv_sec;
    printf("\033[38;5;112mtime = %Lf\n\033[m", en - st);
    long double t2 = en-st;

    printf("Running \033[38;5;202mnormal_mergesort\033[m for n = %lld\n", n);
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    st = ts.tv_nsec/(1e9)+ts.tv_sec;

    // normal mergesort
    normal_mergesort(brr, 0, n-1);
    for(int i=0; i<n; i++)
    {
        printf("%d ",brr[i]);
    }
    printf("\n");
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    en = ts.tv_nsec/(1e9)+ts.tv_sec;
    printf("\033[38;5;202mtime = %Lf\n\033[m", en - st);
    long double t3 = en - st;

    printf("\033[38;5;202mnormal_quicksort\033[m ran:\n\t[ \033[38;5;124m%Lf\033[m ]"
    " times faster than \033[38;5;51mconcurrent_quicksort\033[m\n\t[ \033[38;5;124m%Lf\033[m ]"
    " times faster than \033[38;5;112mthreaded_concurrent_quicksort\033[m\n\n\n", t1/t3, t2/t3);
    detach_memory_block(arr);
    return;
}

int main()
{
    long long int n;
    scanf("%lld", &n);
    runSorts(n);
    destroy_memory_block();
    return 0;
}
