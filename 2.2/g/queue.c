#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>

#include "queue.h"

void *qmonitor(void *arg) {
	int err;
	queue_t *q = (queue_t *)arg;
	printf("qmonitor: [%d %d %d]\n", getpid(), getppid(), gettid());

	while (1) {
		err = sem_wait(&q->queue_lock);
		if (err != SUCCESS) {
			printf("qmonitor: sem_wait(queue_lock) failed: %s\n", strerror(err));
			break;
		}
		queue_print_stats(q);
		err = sem_post(&q->queue_lock);
		if (err != SUCCESS) {
			printf("qmonitor: sem_post(queue_lock) failed: %s\n", strerror(err));
			break;
		}
		sleep(1);
	}
	return NULL;
}

queue_t* queue_init(int max_count) {
	int err;
	queue_t *q = malloc(sizeof(queue_t));
	if (q == NULL) {
		printf("Cannot allocate memory for a queue\n");
		abort();
	}

	q->first = NULL;
	q->last = NULL;
	q->max_count = max_count;
	q->count = 0;
	q->add_attempts = q->get_attempts = 0;
	q->add_count = q->get_count = 0;

	err = sem_init(&q->empty_slots, SEMAPHORE_PRIVATE, max_count);
	if (err != SUCCESS) {
		printf("queue_init: sem_init(empty_slots) failed: %s\n", strerror(err));
		free(q);
		return NULL;
	}
	
	err = sem_init(&q->filled_slots, SEMAPHORE_PRIVATE, 0);
	if (err != SUCCESS) {
		printf("queue_init: sem_init(filled_slots) failed: %s\n", strerror(err));
		err = sem_destroy(&q->empty_slots);
		if (err != SUCCESS) printf("queue_init: sem_destroy(empty_slots) failed: %s\n", strerror(err));
		free(q);
		return NULL;
	}
	err = sem_init(&q->queue_lock, SEMAPHORE_PRIVATE, 1);
	if (err != SUCCESS) {
		printf("queue_init: sem_init(queue_lock) failed: %s\n", strerror(err));
		err = sem_destroy(&q->empty_slots);
		if (err != SUCCESS) printf("queue_init: sem_destroy(empty_slots) failed: %s\n", strerror(err));
		err = sem_destroy(&q->filled_slots);
		if (err != SUCCESS) printf("queue_init: sem_destroy(filled_slots) failed: %s\n", strerror(err));
		free(q);
		return NULL;
	}

	err = pthread_create(&q->qmonitor_tid, NULL, qmonitor, q);
	if (err != SUCCESS) {
		printf("queue_init: pthread_create() failed: %s\n", strerror(err));
		err = sem_destroy(&q->empty_slots);
		if (err != SUCCESS) printf("queue_init: sem_destroy(empty_slots) failed: %s\n", strerror(err));
		err = sem_destroy(&q->filled_slots);
		if (err != SUCCESS) printf("queue_init: sem_destroy(filled_slots) failed: %s\n", strerror(err));
		err = sem_destroy(&q->queue_lock);
		if (err != SUCCESS) printf("queue_init: sem_destroy(queue_lock) failed: %s\n", strerror(err));
        free(q);
		return NULL;
	}
	return q;
}

void queue_destroy(queue_t *q) {
	if (q == NULL) return;

	int err;
	err = pthread_cancel(q->qmonitor_tid);
    if (err != SUCCESS) {
        printf("queue_destroy: pthread_cancel() failed: %s\n", strerror(err));
    }	
	err = pthread_join(q->qmonitor_tid, NULL);
	if (err != SUCCESS) {
		printf("queue_destroy: pthread_join() failed: %s\n", strerror(err));
	}
	err = sem_destroy(&q->empty_slots);
    if (err != SUCCESS) {
        printf("queue_destroy: sem_destroy(empty_slots) failed: %s\n", strerror(err));
    }
    err = sem_destroy(&q->filled_slots);
    if (err != SUCCESS) {
        printf("queue_destroy: sem_destroy(filled_slots) failed: %s\n", strerror(err));
    }
    err = sem_destroy(&q->queue_lock);
    if (err != SUCCESS) {
        printf("queue_destroy: sem_destroy(queue_lock) failed: %s\n", strerror(err));
    }
	
	qnode_t *current = q->first;
	while(current != NULL) {
		qnode_t *tmp = current;
        current = current->next;
        free(tmp);
	}
	free(q);
}

int queue_add(queue_t *q, int val) {
	if (q == NULL) return QUEUE_ERROR;

	int err;	
	q->add_attempts++;
	qnode_t *new = malloc(sizeof(qnode_t));
	if (new == NULL) {
		printf("Cannot allocate memory for new node\n");		
		return QUEUE_ERROR;
	}
	int old_cancel_state;
	err = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancel_state);
	if (err != SUCCESS) {
		printf("queue_add: pthread_setcancelstate() failed: %s\n", strerror(err)); 
		free(new);
		return QUEUE_ERROR;
	}
	err = sem_wait(&q->empty_slots);
    if (err != SUCCESS) {
        printf("queue_add: sem_wait(empty_slots) failed: %s\n", strerror(err));
		err = pthread_setcancelstate(old_cancel_state, NULL); 
		if (err != SUCCESS) printf("queue_add: pthread_setcancelstate() failed: %s\n", strerror(err)); 
		free(new);	
        return QUEUE_ERROR;
    }
	err = sem_wait(&q->queue_lock);
    if (err != SUCCESS) {
        printf("queue_add: sem_wait(queue_lock) failed: %s\n", strerror(err));
		err = sem_post(&q->empty_slots);
		if (err != SUCCESS) printf("queue_add: sem_post(empty_slots) failed: %s\n", strerror(err));
		err = pthread_setcancelstate(old_cancel_state, NULL); 
		if (err != SUCCESS) printf("queue_add: pthread_setcancelstate() failed: %s\n", strerror(err)); 
		free(new);
        return QUEUE_ERROR;
    }

	new->val = val;
	new->next = NULL;
	if (!q->first)
		q->first = q->last = new;
	else {
		q->last->next = new;
		q->last = q->last->next;
	}
	q->count++;
	q->add_count++;

	err = sem_post(&q->queue_lock);
	if (err != SUCCESS) {
		printf("queue_add: sem_post(queue_lock) failed: %s\n", strerror(err));
	}
	err = sem_post(&q->filled_slots);
    if (err != SUCCESS){
        printf("queue_add: sem_post(filled_slots) failed: %s\n", strerror(err));
	}
	err = pthread_setcancelstate(old_cancel_state, NULL);
	if (err != SUCCESS)
		printf("queue_add: pthread_setcancelstate() failed: %s\n", strerror(err));
	return QUEUE_SUCCESS;
}

int queue_get(queue_t *q, int *val) {
	if (q == NULL) return QUEUE_ERROR;
	
	int err;
	q->get_attempts++;
	int old_cancel_state;	
	err = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancel_state);
	if (err != SUCCESS) {
		printf("queue_get: pthread_setcancelstate() failed: %s\n", strerror(err)); 
		return QUEUE_ERROR;
	}
    err = sem_wait(&q->filled_slots);
    if (err != SUCCESS) {
        printf("queue_get: sem_wait(filled_slots) failed: %s\n", strerror(err));
		err = pthread_setcancelstate(old_cancel_state, NULL); 
		if (err != SUCCESS) printf("queue_get: pthread_setcancelstate() failed: %s\n", strerror(err)); 
        return QUEUE_ERROR;
    }
    err = sem_wait(&q->queue_lock);
    if (err != SUCCESS) {
        printf("queue_get: sem_wait(queue_lock) failed: %s\n", strerror(err));
		err = sem_post(&q->empty_slots);
		if (err != SUCCESS) printf("queue_get: sem_post(filled_slots) failed: %s\n", strerror(err));
		err = pthread_setcancelstate(old_cancel_state, NULL); 
		if (err != SUCCESS) printf("queue_get: pthread_setcancelstate() failed: %s\n", strerror(err)); 
        return QUEUE_ERROR;
    }

	qnode_t *tmp = q->first;
	*val = tmp->val;
	q->first = q->first->next;
	if (q->first == NULL) q->last = NULL;
	free(tmp);
	q->count--;
	q->get_count++;

	err = sem_post(&q->queue_lock);
	if (err != SUCCESS) {
		printf("queue_get: sem_post(queue_lock) failed: %s\n", strerror(err));
	}
	err = sem_post(&q->empty_slots);
    if (err != SUCCESS){
        printf("queue_get: sem_post(empty_slots) failed: %s\n", strerror(err));
	}
	err = pthread_setcancelstate(old_cancel_state, NULL);
	if (err != SUCCESS) {
		printf("queue_get: pthread_setcancelstate() failed: %s\n", strerror(err)); 
		return QUEUE_ERROR;
	}
	return QUEUE_SUCCESS;
}

void queue_print_stats(queue_t *q) {
	printf("queue stats: current size %d; attempts: (%ld %ld %ld); counts (%ld %ld %ld)\n",
		q->count,
		q->add_attempts, q->get_attempts, q->add_attempts - q->get_attempts,  //попытки
		q->add_count, q->get_count, q->add_count -q->get_count);
}
