#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bits/pthreadtypes.h>

#define SUCCESS 0
#define ERROR (-1)

void* thread1_block_all() {
    printf("thread1_block_all: TID=%d\n", gettid());
    int err;
    sigset_t all;

    sigfillset(&all);
    err = pthread_sigmask(SIG_BLOCK, &all, NULL);
    if (err != SUCCESS) {
        printf("thread1_block_all: pthread_sigmask failed: %s\n", strerror(err));
        return (void *)ERROR;
    }

    while(1) {
        sleep(1);
    }
    return NULL;
}

void sigint_handler(int sig) {
    if (sig != SIGINT)
        return;
    const char msg[] = "sigint handler: received SIGINT\n";
    write(STDOUT_FILENO, msg, strlen(msg) * sizeof(char));
}

void *thread2_handle_sigint() {
    printf("thread2_handle_sigint: TID=%d\n", gettid());
    int err;
    sigset_t unblock_sigint;

    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    err = sigaction(SIGINT, &sa, NULL);
    if (err == ERROR) {
        printf("thread2_handle_sigint: sigaction failed: %s\n", strerror(errno));
        return (void *)ERROR;
    }

    sigemptyset(&unblock_sigint);
    err = sigaddset(&unblock_sigint, SIGINT);
    if (err == ERROR) {
        printf("thread2_handle_sigint: sigaddset failed: %s\n", strerror(errno));
        return (void *)ERROR;
    }
    err = pthread_sigmask(SIG_UNBLOCK, &unblock_sigint, NULL);
    if (err != SUCCESS) {
        printf("thread2_handle_sigint: pthread_sigmask failed: %s\n", strerror(err));
        return (void *)ERROR;
    }

    while(1) {
        sleep(1);
    }
    return NULL;
}

void* thread3_sigwait() {
    printf("thread3_sigwait: TID=%d\n", gettid());
    int err;
    sigset_t waitset;
    int sig = 0;

    sigemptyset(&waitset);
    err = sigaddset(&waitset, SIGQUIT);
    if (err == ERROR) {
        printf("thread3_sigwait: sigaddset(SIGQUIT) failed: %s\n", strerror(err));
        return (void *)ERROR;
    }

    printf("thread3_sigwait: waiting for SIGQUIT\n");
    err = sigwait(&waitset, &sig);
    if (err == ERROR) {
        printf("thread3_sigwait: sigwait failed: %s\n", strerror(err));
        return (void *)ERROR;
    }
    printf("thread3_sigwait: sigwait received signal %d\n", sig);
    return NULL;
}

int create_mask_block(sigset_t *mask) {
    int err;
    if (mask == NULL) {
        fprintf(stderr, "block_sigint_and_sigquit: mask is NULL\n");
        return ERROR;
    }
    sigemptyset(mask);

    err = sigaddset(mask, SIGINT);
    if (err == ERROR) {
        fprintf(stderr, "sigaddset(SIGINT) failed: %s\n", strerror(errno));
        return ERROR;
    }
    err = sigaddset(mask, SIGQUIT);
    if (err == ERROR) {
        fprintf(stderr, "sigaddset(SIGQUIT) failed: %s\n", strerror(errno));
        return ERROR;
    }
    err = pthread_sigmask(SIG_BLOCK, mask, NULL);
    if (err != SUCCESS) {
        fprintf(stderr, "pthread_sigmask failed: %s\n", strerror(err));
        return ERROR;
    }

    return SUCCESS;
}

int main() {
    printf("main: PID=%d, PPID=%d, TID=%d\n", getpid(), getppid(), gettid());
    int err;
    pthread_t thread1, thread2, thread3;

    sigset_t mask;
    err = create_mask_block(&mask);
    if (err == ERROR) {
        return EXIT_FAILURE;
    }

    err = pthread_create(&thread1, NULL, thread1_block_all, NULL);
    if (err != SUCCESS) {
        printf("main: pthread_create(t1) failed: %s\n", strerror(err));
        return EXIT_FAILURE;
    }
    err = pthread_create(&thread2, NULL, thread2_handle_sigint, NULL);
    if (err != SUCCESS) {
        printf("main: pthread_create(t2) failed: %s\n", strerror(err));
        return EXIT_FAILURE;
    }
    err = pthread_create(&thread3, NULL, thread3_sigwait, NULL);
    if (err != SUCCESS) {
        printf("main: pthread_create(t3) failed: %s\n", strerror(err));
        return EXIT_FAILURE;
    }

    printf("main: threads created. Commands to send signals:\n");
    printf("Ctrl+C or kill -SIGINT %d\n", getpid());
    printf("Ctrl+\\ or kill -SIGQUIT %d\n", getpid());

    sleep(100);
    return EXIT_SUCCESS;
}
