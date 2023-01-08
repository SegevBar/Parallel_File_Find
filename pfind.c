#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

/* NOTE : some of the implementations (variable names, imports, functions) are based on recitations 8 and 9 code files */

/* Structs */
typedef struct Qnode {
    void* value;
    struct Qnode* next;
} Qnode;

typedef struct Queue {
    Qnode* head;
    Qnode* tail;
    int length;
    int offset;
} Queue;

typedef struct ThreadData {
    int id;
    cnd_t cnd;
    int index;
} ThreadData;

/* Global Varibles */
Queue* DIRECTORY_Q;
Queue* THREAD_Q;

int NUM_THREADS;
const char* term;
atomic_int thread_counter = 0;
atomic_int matches = 0;
atomic_int thread_encountered_error = 0;

mtx_t lock;
mtx_t thread_q_lock;
mtx_t dir_q_lock;
cnd_t init_threads;
cnd_t* CND_TO_THREAD;

/* Prototypes */
void init_queue(Queue* q);
Qnode* init_new_dir_qnode(char* value);
Qnode* init_new_thread_qnode(ThreadData* value);
int queue_is_empty(Queue* q);
void enqueue(Queue* q, Qnode* node);
int dequeue(Queue* q);
void initialize_all();
void destroy_all();
void lock_mutex();
void unlock_mutex();
void launch_threads(thrd_t* thread);
int search_directories(void *t);
int search_dirent_iterativly(char* path);


int main(int argc, char const *argv[]) {
    char* path;
    DIR* root_dir;
    Qnode* root_dir_node;
    int rc;

    if (argc != 4) { perror("ERROR - invalid number of arguments"); exit(1); }
    
    /* get current path from cmd args */
    path = (char*) malloc(PATH_MAX);
    if (path == NULL) { 
        perror("ERROR while allocating memmory using malloc function");
        exit(1);
    }
    strcpy(path, argv[1]);
    /* get search term from cmd args */
    term = argv[2];
    /* get the number of threads from cmd args */
    NUM_THREADS = atoi(argv[3]);

    /* init DIRECTORY_Q and THREAD_Q*/
    init_queue(DIRECTORY_Q);
    init_queue(THREAD_Q);
    /* init threads array */
    thrd_t thread[NUM_THREADS];
    /* init condition per thread array */
    CND_TO_THREAD = (cnd_t*) malloc(sizeof(cnd_t)*NUM_THREADS);
    if (CND_TO_THREAD == NULL) {
        perror("ERROR while allocating memory for CND_TO_THREAD, using malloc");
        exit(1);
    }
    /* open root directory */
    if (access(path, R_OK | X_OK) != 0 ) {
        perror("ERROR in access root directory - you don't have permissions to open it");
        exit(1);
    } 
    root_dir = opendir(path);
    /* add root directory to Directories queue */
    root_dir_node = init_new_dir_qnode(path);
    if (thread_encountered_error == 1) { exit(1); }
    enqueue(DIRECTORY_Q, root_dir_node);

    initialize_all();
    
    /* Wait for all searching threads to be created */
    launch_threads(thread);
    /* run search threads */
    for (int t = 0; t < NUM_THREADS; ++t) {
        rc = thrd_join(thread[t], NULL);
        if (rc != thrd_success) {
            perror("ERROR in thrd_join()\n");
            exit(1);
        }
    }
    printf("Done searching, found %d files\n", matches);
    closedir(root_dir);

    /* Clean up and exit */
    destroy_all();
    if (thread_encountered_error) {
        thrd_exit(1);
    }
    thrd_exit(0);
} 


/*
* Given a pointer to a Queue variable, allocate Queue memory and 
* initiate Queue variables
*
*/
void init_queue(Queue* q) {
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
    q->offset = 0;
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

    return new_qnode;
}

/*
* Given a pointer of a pointer of value, allocate memory 
* and initiate thread Qnode variables
* 
*/
Qnode* init_new_thread_qnode(ThreadData* value) {
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
    
    return new_qnode;
}

/*
* init struct variables of ThreadData by thresd id
*
*/
ThreadData* init_thread_data(int id) {
    ThreadData* thread_data;
    ThreadData* tail_data;

    thread_data = (ThreadData*) malloc(sizeof(ThreadData));
    if (thread_data == NULL) {
        perror("ERROR while allocating memory for ThreadData, using malloc");
        thread_encountered_error = 1;
    }
    thread_data->id = id;
    thread_data->cnd = CND_TO_THREAD[id];
    cnd_init(&(thread_data->cnd));
    
    if (queue_is_empty(THREAD_Q)) {
        thread_data->index = 0;
    } else {
        tail_data = (ThreadData*) (THREAD_Q->tail->value);
        thread_data->index = tail_data->index + 1;
    }
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
    }
    q->tail = node;
    q->length++;
}

/*
* If the queue is empty - error
* else - get the head node of the queue and update the head accordinf to
* weather there is only one last node remains in the queue
*/
int dequeue(Queue* q) {
    Qnode* first_node;

    if (queue_is_empty(q)) {
        return -1;
    }
    first_node = q->head;
    q->head = (q->length > 1) ? q->head->next : NULL;
    q->length--;
    q->offset++;
    free(first_node);
    return 0;
}

/*
* runs mtx_init(&lock, mtx_plain) and checks errors
*
*/
void initialize_all() {
    mtx_init(&lock, mtx_plain);
    mtx_init(&thread_q_lock, mtx_plain);
    mtx_init(&dir_q_lock, mtx_plain);
    cnd_init(&init_threads);
}

void destroy_all() {
    int i = 0;
    
    mtx_destroy(&lock);
    mtx_destroy(&dir_q_lock);
    mtx_destroy(&thread_q_lock);
    
    cnd_destroy(&init_threads);
    for (i = 0; i < NUM_THREADS; i++) {
        cnd_destroy(&(CND_TO_THREAD[i]));
    }
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

    mtx_lock(&lock);
    for (t = 0; t < NUM_THREADS; ++t) {
        rc = thrd_create(&thread[t], search_directories, (void *)thread[t]);
        if (rc != thrd_success) {
            perror("ERROR in thrd_create()");
            exit(1);
        }
    }
    cnd_wait(&init_threads, &lock);
    // cnd_broadcast(&init_threads);
    mtx_unlock(&lock);
}

/*
* The function executed by the program threads
* Searches the directories in the Directories_Q
*
*/
int search_directories(void *t) {
    long tid = (long)t;
    Qnode* thread_qnode;
    ThreadData* thread_data;
    ThreadData* head_thread_data;
    int curr_head_id;
    char* path = (char*) malloc(PATH_MAX);

    /* wait for all thread to be created, last thread wakes everyone up */
    mtx_lock(&lock);
    thread_counter++;
    (thread_counter != NUM_THREADS) ? cnd_wait(&init_threads, &lock) :                                 cnd_broadcast(&init_threads);
    mtx_unlock(&lock);

    while(1) {       
        mtx_lock(&thread_q_lock);
        thread_qnode = init_new_thread_qnode(init_thread_data(tid));
        enqueue(THREAD_Q, thread_qnode);

        /* exit thread when there are no more directories to search */
        mtx_lock(&dir_q_lock);
        if (queue_is_empty(DIRECTORY_Q) && THREAD_Q->length == NUM_THREADS) {
            mtx_unlock(&thread_q_lock);
            mtx_unlock(&dir_q_lock);
            thrd_exit(0);
        }
        /* current thread is not the head in THREAD_Q or there are not enough dirs in DIRECTORY_Q -> going to sleep */
        head_thread_data = (ThreadData*)(THREAD_Q->head->value);
        curr_head_id = head_thread_data->id;
        thread_data = (ThreadData*)(thread_qnode->value);
        if ((curr_head_id != tid) || (DIRECTORY_Q->length < thread_data->index - THREAD_Q->offset)) { 
            cnd_wait(&(thread_data->cnd), &thread_q_lock);
        }
        /* current thread had a directory in DIRECTORY_Q but it is not the head yet -> waiting until it becomes the head */
        while (curr_head_id != tid) {
            mtx_unlock(&thread_q_lock);
            mtx_lock(&thread_q_lock);
            head_thread_data = (ThreadData*)(THREAD_Q->head->value);
            curr_head_id = head_thread_data->id;
        }
        /* current thread is the head of THREAD_Q but DIRECTORY_Q is empty */
        mtx_lock(&dir_q_lock);
        if (queue_is_empty(DIRECTORY_Q)) {
            mtx_unlock(&thread_q_lock);
            cnd_wait(&(thread_data->cnd), &dir_q_lock);
            mtx_lock(&thread_q_lock);
        } 
        /* current thread is the head of THREAD_Q and there is a directory in DIRECTORY_Q assigned to it */
        strcpy(path, ((char*) DIRECTORY_Q->head->value));
        dequeue(DIRECTORY_Q);
        dequeue(THREAD_Q);
        mtx_unlock(&thread_q_lock);
        mtx_unlock(&dir_q_lock);
        
        search_dirent_iterativly(path);
    }
    return tid;
}

/*
* opens current directory by path var
* iterate through dirents in path
* if dirent is directory -> add to DIRECTORY_Q and send a signal to first thread in THREAD_Q 
* else -> checks term
*/
int search_dirent_iterativly(char* path) {
    DIR* dir;
    struct dirent* dirent;
    char* full_path;
    struct stat stat_data;
    Qnode* new_dir;
    ThreadData* thread_head_data;

    if (access(path, R_OK | X_OK) != 0) {
        printf("Directory %s: Permission denied.\n", path);
        thread_encountered_error = 1;
        return -1;
    } 
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
            return -1;
        }
        sprintf(full_path, "%s/%s", path, dirent->d_name);

        /* get the stats of full_path */
        if (stat(full_path, &stat_data) == -1) {  
            perror("An error has occurred in stat function");
            return -1;
        }
        /* add directory to DIRECTORY_Q and send a signal to wake first in line thread */
        if (S_ISDIR(stat_data.st_mode)) {  /*S_ISDIR is nonzero for directories*/
            new_dir = init_new_dir_qnode(full_path);
            mtx_lock(&dir_q_lock);
            enqueue(DIRECTORY_Q, new_dir);
            mtx_lock(&thread_q_lock);
            if (!queue_is_empty(THREAD_Q)) {
                thread_head_data = (ThreadData*)(THREAD_Q->head->value);
                cnd_signal(&(thread_head_data->cnd));
            }
            mtx_unlock(&thread_q_lock);
            mtx_unlock(&dir_q_lock);
        
        /* check if file contains term */
        } else {
            if (strstr(dirent->d_name, term) != NULL) {
                printf("%s\n", full_path);
                matches++;
            }
        }
    }
    closedir(dir);
    return 0;
}
