#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>

/* NOTE : some of the implementations (variable names, imports, functions) are based on recitations 8 and 9 code files */

/* Structs */
typedef struct Qnode {
    void* value;
    struct Qnode* next;
    struct Qnode* prev;
    int index;
} Qnode;

typedef struct Queue {
    Qnode* head;
    Qnode* tail;
    int length;
    int count;
} Queue;

typedef struct ThreadData {
    int* id;
    cnd_t cnd;
} ThreadData;

/* Global Varibles */
Queue* DIRECTORY_Q;
Queue* THREAD_Q;

int NUM_THREADS;
const char* TERM;
atomic_int thread_counter = 0;
atomic_int thread_index = 0;
atomic_int MATCHES = 0;
atomic_int thread_encountered_error = 0;
atomic_int EXIT_THREADS = 0;

mtx_t lock;

cnd_t wake_all_threads;
cnd_t finish_init_threads;
cnd_t* CND_TO_THREAD;

/* Prototypes */
Queue* init_queue();
Qnode* init_new_dir_qnode(char* value);
Qnode* init_new_thread_qnode(ThreadData* value, int index);
ThreadData* init_thread_data(int* id);
int queue_is_empty(Queue* q);
void enqueue(Queue* q, Qnode* node);
int dequeue(Queue* q, int i, int flag);
void* get_value(Queue* q, int i);
void initialize_all();
void destroy_all();
void free_queue(Queue* q, int flag);
void lock_mutex();
void unlock_mutex();
void launch_threads(thrd_t* thread);
int search_directories(void *t);
int search_dirent_iterativly(char* path, int index);
void wakeup_all_threads();


int main(int argc, char const *argv[]) {
    char* path;
    DIR* root_dir;
    Qnode* root_dir_node;
    int i;

    if (argc != 4) { perror("ERROR - invalid number of arguments"); exit(1); }
    
    /* get current path from cmd args */
    path = (char*) malloc(PATH_MAX);
    if (path == NULL) { 
        perror("ERROR while allocating memmory using malloc function");
        exit(1);
    }
    strcpy(path, argv[1]);
    if (access(path, R_OK | X_OK) != 0 ) {
        perror("ERROR in access root directory - you don't have permissions to search in it");
        exit(1);
    } 
    /* get search term from cmd args */
    TERM = argv[2];
    /* get the number of threads from cmd args */
    NUM_THREADS = atoi(argv[3]);

    /* init DIRECTORY_Q and THREAD_Q*/
    DIRECTORY_Q = init_queue();
    THREAD_Q = init_queue();
    /* init threads array */
    thrd_t thread[NUM_THREADS];
    /* init all mutex and conditions */
    initialize_all();

    /* open root directory */
    root_dir = opendir(path);
    /* add root directory to Directories queue */
    root_dir_node = init_new_dir_qnode(path);
    
    if (thread_encountered_error == 1) { exit(1); }
    enqueue(DIRECTORY_Q, root_dir_node);

    /* Wait for all searching threads to be created */
    launch_threads(thread);
    /* run search threads */
    for (i = 0; i < NUM_THREADS; i++) {
        thrd_join(thread[i], NULL);
    }

    closedir(root_dir);
    free(path);

    printf("Done searching, found %d files\n", MATCHES);

    /* Clean up and exit */
    destroy_all();
    if (thread_encountered_error) {
        exit(1);
    }
    exit(0);
    return 0;
} 


/*
* Given a pointer to a Queue variable, allocate Queue memory and 
* initiate Queue variables
*
*/
Queue* init_queue() {
    Queue* q;

    /* allocate memory */
    q = (Queue*) malloc(sizeof(Queue));
    if (q == NULL) {
        perror("ERROR while allocating memory for Queue, using malloc");
        exit(1);
    }
    /* init Queue parameters */
    q->head = NULL;
    q->tail = NULL;
    q->length = 0;
    q->count = 0;

    return q;
}

/*
* Given a pointer of a pointer of value, allocate memory and 
* initiate Directory Qnode variables
* 
*/
Qnode* init_new_dir_qnode(char* value) {
    Qnode* new_qnode;
    
    new_qnode = (Qnode*) malloc(sizeof(Qnode));
    if (new_qnode == NULL) {
        perror("ERROR while allocating memory for Directory Qnode, using malloc");
        thread_encountered_error = 1;
    }
    
    new_qnode->value = (char*) malloc(PATH_MAX);
    if (new_qnode == NULL) {
        perror("ERROR while allocating memory for Qnode value, using malloc");
        thread_encountered_error = 1;
    }
    strcpy(new_qnode->value, value);
    new_qnode->next = NULL;
    new_qnode->prev = NULL;
    new_qnode->index = DIRECTORY_Q->count + 1;
    free(value);
    return new_qnode;
}

/*
* Given a pointer of a pointer of value, allocate memory 
* and initiate thread Qnode variables
* 
*/
Qnode* init_new_thread_qnode(ThreadData* value, int index) {
    Qnode* new_qnode;
    
    new_qnode = (Qnode*) malloc(sizeof(Qnode));
    if (new_qnode == NULL) {
        perror("ERROR while allocating memory for Qnode, using malloc");
        thread_encountered_error = 1;
    }
    
    new_qnode->value = (ThreadData*) malloc(sizeof(ThreadData*));
    if (new_qnode == NULL) {
        perror("ERROR while allocating memory for Qnode value, using malloc");
        thread_encountered_error = 1;
    }
    new_qnode->value = value;
    new_qnode->next = NULL;
    new_qnode->prev = NULL;
    new_qnode->index = index;
    
    return new_qnode;
}

/*
* init struct variables of ThreadData by thresd id
*
*/
ThreadData* init_thread_data(int* id) {
    ThreadData* thread_data;

    thread_data = (ThreadData*) malloc(sizeof(ThreadData));
    if (thread_data == NULL) {
        perror("ERROR while allocating memory for ThreadData, using malloc");
        thread_encountered_error = 1;
    }
    thread_data->id = id;
    thread_data->cnd = CND_TO_THREAD[*id];

    return thread_data;
}

/*
* Checks if the provided queue is empty.
* if empty - returns 1, else returns 0
*
*/
int queue_is_empty(Queue* q) {
    if (q->length == 0) {
        return 1;
    }
    return 0;
}

/*
* If the queue is empty - add the node to be both head and tail 
* else - add the node at the tail
* updates nodes count
*/
void enqueue(Queue* q, Qnode* node) {
    
    if (queue_is_empty(q)) {
        q->head = node;
    } else {
        q->tail->next = node;
        node->prev = q->tail;
    }
    q->tail = node;
    q->length++;
    q->count++;
}

/*
* If the queue is empty - error
* else - get the head node of the queue and update the head accordinf to
* weather there is only one last node remains in the queue
*/
int dequeue(Queue* q, int i, int flag) {
    Qnode* node;

    if (queue_is_empty(q)) {
        return -1;
    }
    node = q->head;
    while (node != NULL && node->index != i) {
        node = node->next;
    }
    if (node == NULL) {
        return -1;
    }
    /* update next and prev pointers */
    if (node->next != NULL) {
        node->next->prev = node->prev;
    }
    if (node->prev != NULL) {
        node->prev->next = node->next;
    }
    /* update head */
    if (node == q->head) { 
        q->head = q->head->next;
    }
    /* update tail */
    if (node == q->tail) { 
        q->tail = q->tail->prev;
    }
    q->length--;
    
    if (flag == 1) {
        ThreadData* data = (ThreadData*) node->value;
        free(data);
    } else {
        char* path = (char*) node->value;
        free(path);
    }
    free(node);
    return 0;
}

/*
* get value of the i node in queue
*
*/
void* get_value(Queue* q, int i) {
    Qnode* node;

    if (queue_is_empty(q)) {
        return NULL;
    }
    node = q->head;
    while (node != NULL && node->index != i) {
        node = node->next;
    }
    if (node == NULL) {
        return NULL;
    }
    return node->value;
}

/*
* runs mtx_init(&lock, mtx_plain) and checks errors
*
*/
void initialize_all() {
    int i;

    mtx_init(&lock, mtx_plain);
    
    cnd_init(&wake_all_threads);
    cnd_init(&finish_init_threads);

    /* init condition per thread array */
    CND_TO_THREAD = (cnd_t*) malloc(NUM_THREADS*sizeof(cnd_t));
    if (CND_TO_THREAD == NULL) {
        perror("ERROR while allocating memory for CND_TO_THREAD, using malloc");
        exit(1);
    }
    for (i = 0; i < NUM_THREADS; i++) {
        cnd_init(&CND_TO_THREAD[i]);
    }
}

void destroy_all() {
    int i = 0;
    
    mtx_destroy(&lock);
    
    cnd_destroy(&wake_all_threads);
    cnd_destroy(&finish_init_threads);

    /* destroy condition per thread array */
    for (i = 0; i < NUM_THREADS; i++) {
        cnd_destroy(&(CND_TO_THREAD[i]));
    }
    free(CND_TO_THREAD);
    free_queue(THREAD_Q, 1);
    free_queue(DIRECTORY_Q, 0);
}

/* 
* free memory allocated to the queue variable
*
*/
void free_queue(Queue* q, int flag) {
    Qnode* node;

    if (!queue_is_empty(q)) {
        node = q->head;
        while (node != NULL) {
            q->head = node->next;
            if (flag == 1) {
                ThreadData* data = (ThreadData*) node->value;
                free(data);
            } else {
                char* path = (char*) node->value;
                free(path);
            }
            free(node);
            node = q->head;
        }
    }
    free(q);
}

/*
* Creates NUM_THREADS threads by the following steps:
* locks the lock,  
* thrd_create(&thread[t], search_directories, (void *)t) for NUM_THREADS threads
* cnd_wait(&init_threads, &lock)
* cnd_broadcast(&init_threads)
* unlocks lock
*/
void launch_threads(thrd_t* thread) {
    int rc;
    int t;
    int* id;

    mtx_lock(&lock);
    for (t = 0; t < NUM_THREADS; t++) {
        /* use index as id */
        id = (int*) malloc(sizeof(int));
        if (id == NULL) {
            perror("ERROR while allocating memory for id, using malloc");
            exit(1);
        }
        *id = t;

        rc = thrd_create(&thread[t], search_directories, (void *)id);
        if (rc != thrd_success) {
            perror("ERROR in thrd_create()");
            exit(1);
        }
    }
    cnd_wait(&finish_init_threads, &lock);
    cnd_broadcast(&wake_all_threads);
    mtx_unlock(&lock);
}

/*
* The function executed by the program threads
* threads without corresponding directory go to sleep 
* thread matches a directory by counter (index) num
*
*/
int search_directories(void *t) {
    int* tid = (int*)t;
    Qnode* thread_qnode;
    ThreadData* thread_data;
    int curr_index;
    char* curr_path;
    char* path = (char*) malloc(PATH_MAX);

    /* wait for all thread to be created, last thread wakes everyone up */
    mtx_lock(&lock);
    thread_counter++;
    (thread_counter != NUM_THREADS) ? cnd_wait(&wake_all_threads, &lock) :                                 cnd_signal(&finish_init_threads);
    mtx_unlock(&lock);

    while(1) {    
        /* exit thread when there are no more directories to search */
        mtx_lock(&lock);
        if (queue_is_empty(DIRECTORY_Q) && THREAD_Q->length >= NUM_THREADS-1) {
            mtx_unlock(&lock);
            EXIT_THREADS = 1;
            wakeup_all_threads();
            free(tid);
            thrd_exit(0);
        }
        curr_index = ++thread_index; 
        mtx_unlock(&lock);

        /* 
        if there are not enouth directories in DIRECTORY_Q -> current thread has no directory to work on
        so thread enqueues to THREAD_Q and goes to sleep
        when a new directory be added to DIRECTORY_Q a signal will wake the thread up
        */
        mtx_lock(&lock);
        if (DIRECTORY_Q->count < curr_index) {
            thread_data = init_thread_data(tid);
            thread_qnode = init_new_thread_qnode(thread_data, curr_index);
            enqueue(THREAD_Q, thread_qnode);
            cnd_wait(&(thread_data->cnd), &lock);
            if (EXIT_THREADS) {  /*if all the job is done - wake up to exit*/
                mtx_unlock(&lock); 
                free(tid);
                thrd_exit(0); 
            }
            dequeue(THREAD_Q, curr_index, 1);
        }        
        /* if there is a directory in DIRECTORY_Q assigned to current thread */
        curr_path = (char*) get_value(DIRECTORY_Q, curr_index);
        strcpy(path, (char*) curr_path);
        mtx_unlock(&lock);
        
        search_dirent_iterativly(curr_path, curr_index);
        
        mtx_lock(&lock);
        dequeue(DIRECTORY_Q, curr_index, 0);
        mtx_unlock(&lock);
    }
    return *tid;
}

/*
* opens current directory by path var
* iterate through dirents in path
* if dirent is directory -> add to DIRECTORY_Q and send a signal to first thread in THREAD_Q 
* else -> checks term
*/
int search_dirent_iterativly(char* path, int index) {
    DIR* dir;
    struct dirent* dirent;
    char* full_path;
    struct stat stat_data;
    Qnode* new_dir;
    ThreadData* thread_data;

    dir = opendir(path);

    while ((dirent = readdir(dir)) != NULL) {
        /* ignore '.' and '..' */
        if ((strcmp(dirent->d_name,".") == 0) || (strcmp(dirent->d_name, "..") == 0)) {
            continue;
        }
        /* get full path */
        full_path = (char*) malloc(PATH_MAX);
        if (full_path == NULL) {
            thread_encountered_error = 1;
            perror("ERROR while allocating memory for full_path, using malloc");
            continue;
        }
        sprintf(full_path, "%s/%s", path, dirent->d_name);

        /* get the stats of full_path */
        if (stat(full_path, &stat_data) == -1) {  
            perror("ERROR in stat() function");
            thread_encountered_error = 1;
            continue;
        }
        /* add directory to DIRECTORY_Q and send a signal to wake first in line thread */
        if (S_ISDIR(stat_data.st_mode)) {  /*S_ISDIR is nonzero for directories*/
            if (access(full_path, R_OK | X_OK) == -1) {
                printf("Directory %s: Permission denied.\n", full_path);
                continue;
            } 
            /* add directory to DIRECTORY_Q */
            mtx_lock(&lock);
            new_dir = init_new_dir_qnode(full_path);
            enqueue(DIRECTORY_Q, new_dir);
            thread_data = get_value(THREAD_Q, new_dir->index);
            /*if the corresponding thread asleep (in THREAD - wake it up */
            if (thread_data != NULL) { 
                cnd_signal(&(((ThreadData*) thread_data)->cnd));
            }
            mtx_unlock(&lock);
        
        /* check if file contains term */
        } else {
            if (strstr(dirent->d_name, TERM) != NULL) {
                printf("%s\n", full_path);
                MATCHES++;
            }
            free(full_path);
        }
    }
    closedir(dir);

    return 0;
}

/*
* wakes up all the thread in THREAD_Q which are sleeping
* this function is called when all the work is done
*/
void wakeup_all_threads() {
    Qnode* node;
    ThreadData* thread_data;

    node = THREAD_Q->head;
    while (node != NULL) {
        thread_data = (ThreadData*) node->value;
        cnd_signal(&(thread_data->cnd));
        node = node->next;
    }
}
