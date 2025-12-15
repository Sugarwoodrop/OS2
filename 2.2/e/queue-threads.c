#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include <pthread.h>
#include <sched.h>

#include "queue.h"

#define RED "\033[41m" 
#define NOCOLOR "\033[0m"

int cancel_and_join_thread(pthread_t thread, char *thread_name) {
	int err;
	err = pthread_cancel(thread);
    if (err != SUCCESS) {
        printf("main: pthread_cancel() failed: %s\n", strerror(err));
		return ERROR;
    }	
	err = pthread_join(thread, NULL);
	if (err != SUCCESS) {
		printf("main: pthread_join() failed: %s\n", strerror(err));
		return ERROR;
	}
	printf("main: %s thread was successfully joined\n", thread_name);
    return SUCCESS;	
}

void set_cpu(int n) {
	int err;
	cpu_set_t cpuset;  
	pthread_t tid = pthread_self();

	CPU_ZERO(&cpuset);  
	CPU_SET(n, &cpuset);  

	err = pthread_setaffinity_np(tid, sizeof(cpu_set_t), &cpuset);  
	if (err != SUCCESS) {
		printf("set_cpu: pthread_setaffinity failed for cpu %d\n", n);
		return;
	}
	printf("set_cpu: set cpu %d\n", n);
}

void *reader(void *arg) {  
	int expected = 0;
	queue_t *q = (queue_t *)arg;
	printf("reader [%d %d %d]\n", getpid(), getppid(), gettid());

	set_cpu(0);

	while (1) {
		pthread_testcancel();
		int val = -1;
		int ok = queue_get(q, &val);
		if (ok != QUEUE_SUCCESS)
			continue;

		if (expected != val)
			printf(RED"ERROR: get value is %d but expected - %d" NOCOLOR "\n", val, expected);

		expected = val + 1;
	}
	return NULL;
}

void *writer(void *arg) {
	int i = 0;
	queue_t *q = (queue_t *)arg;
	printf("writer [%d %d %d]\n", getpid(), getppid(), gettid());

	set_cpu(1);

	while (1) {
		pthread_testcancel();
		int ok = queue_add(q, i);
		if (ok != QUEUE_SUCCESS) {
			//usleep(1);
			continue;			
		}
		i++;
		//usleep(1);
	}
	return NULL;
}

int main() {
	pthread_t reader_tid, writer_tid;
	queue_t *q;
	int err;
	printf("main [%d %d %d]\n", getpid(), getppid(), gettid());
	q = queue_init(1000000);
	if (q == NULL) {  
        printf(RED"ERROR: Failed to initialize queue" NOCOLOR "\n");
        return ERROR;
    }

	err = pthread_create(&reader_tid, NULL, reader, q);
	if (err != SUCCESS) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		queue_destroy(q);
		return ERROR;
	}

	sched_yield();  

	err = pthread_create(&writer_tid, NULL, writer, q);
	if (err != SUCCESS) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		cancel_and_join_thread(reader_tid, "reader");
		queue_destroy(q);
		return ERROR;
	}
	sleep(10);
	int result;
	result = cancel_and_join_thread(reader_tid, "reader");
	if (result != SUCCESS) {
		return ERROR;
	}
	result = cancel_and_join_thread(writer_tid, "writer");
	if (result != SUCCESS) {
		return ERROR;
	}
	queue_destroy(q);
	printf("main: queue was destroyed\n");
	return SUCCESS;
}
