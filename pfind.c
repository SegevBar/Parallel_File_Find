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
} Queue;

/* Global Varibles */
Queue* DIRECTORY_Q;
Queue* THREADS_Q;


/* Prototypes */




int main(int argc, char const *argv[]) {
    char* path;
    char* term;
    int threads_num;
    int* id;
    Qnode* dir;
    DIR* root_dir;
    int i;

    if (argc != 4) { perror("Invalid number of arguments"); exit(1); }
    
    /* get current path from cmd args */
    path = (char*) malloc(PATH_MAX);
    if (path == NULL) { 
        perror("An error occurred while allocating memmory using malloc function");
        exit(1);
    }
    strcpy(path, argv[1]);
    /* get search term from cmd args */
    term = argv[2];
    /* get the number of threads from cmd args */
    threads_num = atoi(argv[3]);

    /* open root directory */
    if (access(path, F_OK) != 0 && access(path, R_OK) != 0 && access(path, X_OK) != 0) {
        perror("An error occurred, can't find root directory or you don't have permissions to open it");
        exit(1);
    } 
    root_dir = opendir(path);

    init_queue(DIRECTORY_Q);
    init_queue(THREADS_Q);



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
        perror("An error accured while allocating memory for Queue, using malloc");
        exit(1);
    }
    /* init Queue parameters */
    q->head = NULL;
    q->tail = NULL;
    q->length = 0;
}

/*
* Given a pointer of a pointer of value, and a flag representing the type 
* of Queue, allocate memory and initiate Qnode variables
* 
*/
Qnode* init_new_qnode(void** value, int flag) {
    Qnode* new_qnode = (Qnode*) malloc(sizeof(Qnode));
    if (new_qnode == NULL) {
        perror("An error accured while allocating memory for Qnode, using malloc");
        exit(1);
    }
    new_qnode->next = NULL;

    switch (flag) { /* flag == 0 -> directory, flag == 1 -> thread */
        case 0:
            new_qnode->value = (char*) malloc(PATH_MAX);
            if (new_qnode == NULL) {
                perror("An error accured while allocating memory for Qnode value, using malloc");
                exit(1);
            }
            strcpy(new_qnode->value, (*value));
            break;
        case 1:
            new_qnode->value = (int*) malloc(sizeof(int));
            if (new_qnode == NULL) {
                perror("An error accured while allocating memory for Qnode value, using malloc");
                exit(1);
            }
            new_qnode->value = (*value);
    }
    return new_qnode;
}

/*
* Checks if the provided queue is empty.
* if empty - returns 1, else returns 0
*
*/
int queue_is_empty(Queue* q) {
    if (q->length == 0) {
        reutrn 1;
    }
    return 0;
}

/*
* If the queue is empty - add the node to be both head and tail 
* else - add the node at the tail
* updates nodes count
*/
void insert_qnode_in_end_of_queue(Queue* q, Qnode* node) {
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
int get_queue_first_in_line_qnode(Queue* q) {
    Qnode first_node;

    if (queue_is_empty(q)) {
        return -1;
    }
    first_node = q->head;
    q->head = (q->length > 1) ? q->head->next : NULL;
    q->length--;
    
    return 0;
}

