#include "shell.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    pid_t pid;
    int job_id;
    char* cmd;
    volatile sig_atomic_t active;
    volatile sig_atomic_t done;
} Job;

static Job jobs[MAX_JOBS];
static int next_job_id = 1;

// Async-signal-safe SIGCHLD handler: reaps only tracked background PIDs
static void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno;
    int status;

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active && !jobs[i].done) {
            pid_t res = waitpid(jobs[i].pid, &status, WNOHANG);
            if (res > 0) {
                jobs[i].done = 1;
            }
        }
    }

    errno = saved_errno;
}

void init_signals(void) {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("sigaction");
    }
}

void add_job(pid_t pid, const char* cmd) {
    sigset_t mask, prev;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &prev);

    int slot = -1;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].active) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        fprintf(stderr, "myshell: maximum background jobs (%d) reached\n", MAX_JOBS);
        sigprocmask(SIG_SETMASK, &prev, NULL);
        return;
    }

    jobs[slot].pid = pid;
    jobs[slot].job_id = next_job_id++;
    jobs[slot].cmd = strdup(cmd ? cmd : "");
    jobs[slot].done = 0;
    jobs[slot].active = 1;

    printf("[%d] %d\n", jobs[slot].job_id, (int)pid);

    sigprocmask(SIG_SETMASK, &prev, NULL);
}

void cleanup_done_jobs(void) {
    sigset_t mask, prev;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &prev);

    int any_active = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active) {
            if (jobs[i].done) {
                printf("[%d]+  Done                    %s\n", jobs[i].job_id, jobs[i].cmd);
                free(jobs[i].cmd);
                jobs[i].cmd = NULL;
                jobs[i].active = 0;
                jobs[i].done = 0;
            } else {
                any_active = 1;
            }
        }
    }

    if (!any_active) {
        next_job_id = 1;
    }

    sigprocmask(SIG_SETMASK, &prev, NULL);
}

void print_jobs(void) {
    cleanup_done_jobs();

    sigset_t mask, prev;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &prev);

    int count = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active && !jobs[i].done) {
            printf("[%d]   Running   %-8d %s\n",
                   jobs[i].job_id, (int)jobs[i].pid, jobs[i].cmd);
            count++;
        }
    }

    if (count == 0) {
        printf("No active background jobs.\n");
    }

    sigprocmask(SIG_SETMASK, &prev, NULL);
}

void free_jobs(void) {
    sigset_t mask, prev;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &prev);

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active) {
            free(jobs[i].cmd);
            jobs[i].cmd = NULL;
            jobs[i].active = 0;
            jobs[i].done = 0;
        }
    }

    sigprocmask(SIG_SETMASK, &prev, NULL);
}
