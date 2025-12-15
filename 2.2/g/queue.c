#define _GNU_SOURCE
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include "queue.h"

void *qmonitor(void *arg) {
    queue_t *q = (queue_t *)arg;
    printf("qmonitor: [%d %d %d]\n", getpid(), getppid(), gettid());

    while (1) {
        if (sem_wait(&q->queue_lock) != 0) {
            printf("qmonitor: sem_wait failed: %s\n", strerror(errno));
            break;
        }
        queue_print_stats(q);
        if (sem_post(&q->queue_lock) != 0) {
            printf("qmonitor: sem_post failed: %s\n", strerror(errno));
            break;
        }
        sleep(1);
    }
    return NULL;
}

queue_t* queue_init(int max_count) {
    queue_t *q = malloc(sizeof(queue_t));
    if (q == NULL) {
        printf("Cannot allocate memory for a queue\n");
        return NULL;
    }

    q->first = NULL;
    q->last = NULL;
    q->max_count = max_count;
    q->count = 0;
    q->add_attempts = q->get_attempts = 0;
    q->add_count = q->get_count = 0;

    if (sem_init(&q->empty_slots, 0, max_count) != 0) {
        printf("queue_init: sem_init(empty_slots) failed: %s\n", strerror(errno));
        free(q);
        return NULL;
    }
    
    if (sem_init(&q->filled_slots, 0, 0) != 0) {
        printf("queue_init: sem_init(filled_slots) failed: %s\n", strerror(errno));
        sem_destroy(&q->empty_slots);
        free(q);
        return NULL;
    }
    
    if (sem_init(&q->queue_lock, 0, 1) != 0) {
        printf("queue_init: sem_init(queue_lock) failed: %s\n", strerror(errno));
        sem_destroy(&q->empty_slots);
        sem_destroy(&q->filled_slots);
        free(q);
        return NULL;
    }

    if (pthread_create(&q->qmonitor_tid, NULL, qmonitor, q) != 0) {
        printf("queue_init: pthread_create() failed: %s\n", strerror(errno));
        sem_destroy(&q->empty_slots);
        sem_destroy(&q->filled_slots);
        sem_destroy(&q->queue_lock);
        free(q);
        return NULL;
    }
    
    return q;
}

void queue_destroy(queue_t *q) {
    if (q == NULL) return;

    pthread_cancel(q->qmonitor_tid);
    pthread_join(q->qmonitor_tid, NULL);
    
    sem_destroy(&q->empty_slots);
    sem_destroy(&q->filled_slots);
    sem_destroy(&q->queue_lock);
    
    qnode_t *current = q->first;
    while (current != NULL) {
        qnode_t *tmp = current;
        current = current->next;
        free(tmp);
    }
    free(q);
}

int queue_add(queue_t *q, int val) {
    if (q == NULL) return QUEUE_ERROR;

    int old_cancel_state;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancel_state);
    
    if (sem_wait(&q->empty_slots) != 0) {
        printf("queue_add: sem_wait(empty_slots) failed: %s\n", strerror(errno));
        pthread_setcancelstate(old_cancel_state, NULL);
        return QUEUE_ERROR;
    }
    
    qnode_t *new = malloc(sizeof(qnode_t));
    if (new == NULL) {
        printf("Cannot allocate memory for new node\n");
        sem_post(&q->empty_slots);
        pthread_setcancelstate(old_cancel_state, NULL);
        return QUEUE_ERROR;
    }
    
    if (sem_wait(&q->queue_lock) != 0) {
        printf("queue_add: sem_wait(queue_lock) failed: %s\n", strerror(errno));
        sem_post(&q->empty_slots);
        free(new);
        pthread_setcancelstate(old_cancel_state, NULL);
        return QUEUE_ERROR;
    }
    
    q->add_attempts++; 
    
    new->val = val;
    new->next = NULL;
    
    if (q->first == NULL)
        q->first = q->last = new;
    else {
        q->last->next = new;
        q->last = new;
    }
    
    q->count++;
    q->add_count++;
    
    sem_post(&q->queue_lock);
    
    sem_post(&q->filled_slots);
    
    pthread_setcancelstate(old_cancel_state, NULL);
    return QUEUE_SUCCESS;
}

int queue_get(queue_t *q, int *val) {
    if (q == NULL) return QUEUE_ERROR;
    
    int old_cancel_state;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancel_state);
    
    if (sem_wait(&q->filled_slots) != 0) {
        printf("queue_get: sem_wait(filled_slots) failed: %s\n", strerror(errno));
        pthread_setcancelstate(old_cancel_state, NULL);
        return QUEUE_ERROR;
    }
    
    if (sem_wait(&q->queue_lock) != 0) {
        printf("queue_get: sem_wait(queue_lock) failed: %s\n", strerror(errno));
        sem_post(&q->filled_slots);
        pthread_setcancelstate(old_cancel_state, NULL);
        return QUEUE_ERROR;
    }
    
    q->get_attempts++;
    
    qnode_t *tmp = q->first;
    *val = tmp->val;
    q->first = q->first->next;
    
    if (q->first == NULL)
        q->last = NULL;
    
    free(tmp);
    q->count--;
    q->get_count++;
    
    sem_post(&q->queue_lock);
    
    sem_post(&q->empty_slots);
    
    pthread_setcancelstate(old_cancel_state, NULL);
    return QUEUE_SUCCESS;
}
