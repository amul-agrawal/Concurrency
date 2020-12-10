# Concurrent MergeSort Report
## How to Perform merge sort ?
Merge Sort is a Divide and Conquer algorithm. It divides input array in two halves, calls itself for the two halves and then merges the two sorted halves. The merge() function is used for merging two halves. The `merge(arr, l, m, r)` is key process that assumes that `arr[l..m]` and `arr[m+1..r]` are sorted and merges the two sorted sub-arrays into one.

Sorting the two smaller halves can be done by following three methods: 
1. Creating a new process for each half. (Forked)
2. Creating a new thread for each half.  (Threaded)
3. In the same thread and same process.  (Normal)

## Shared memory
Forked merge sort requires sharing memory between processes. 
Shared memory has 4 parts:
1. Creating a shared memory.
```c
#define IPC_RESULT_ERROR (-1)
int shared_block_id;

void get_shared_block(int size) {
    key_t key = IPC_PRIVATE;
    shared_block_id = shmget(key, size, 0644 | IPC_CREAT);
}
```
2. Attaching it to a process.
```c
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
```
3. Detaching it from a process.
```c
bool detach_memory_block(int *block) {
    return (shmdt(block) != IPC_RESULT_ERROR);
}
```
4. Deleting it.
```c
bool destroy_memory_block() {
    if(shared_block_id == IPC_RESULT_ERROR) {
        return NULL;
    }
    return (shmctl(shared_block_id, IPC_RMID, NULL) != IPC_RESULT_ERROR);
}
```
## Performance Comparison 
1. `N = 5`
```
forked: time = 0.000522 
threaded: time = 0.000341
normal: time = 0.000009
normal_quicksort ran:
	[ 56.040074 ] times faster than concurrent_quicksort
	[ 36.606898 ] times faster than threaded_concurrent_quicksort
```
2. `N = 50`
```
forked: time = 0.001605 
threaded: time = 0.004434
normal: time = 0.000020
normal_quicksort ran:
	[ 82.212325 ] times faster than concurrent_quicksort
	[ 227.129700 ] times faster than threaded_concurrent_quicksort
```
3. `N = 5000`
```
forked: time = 0.229146 
threaded: time = 0.087758
normal: time = 0.001412
normal_quicksort ran:
	[ 162.252827 ] times faster than concurrent_quicksort
	[ 62.139247 ] times faster than threaded_concurrent_quicksort
```
4. `N = 500000`
My System gave up here.


## Conclusion
Rank of different methods on the basis of speed of execution.
1. normal
2. threaded
3. forked

Reason for this is because of large overhead of creating a process/thread which nullifies the benifit of multithread/multiprocess. Multithreaded mergesort usually runs faster than multiprocess mergesort because creating threads is faster than creating processes as threads don't have a separate PCB and share several memory regions.