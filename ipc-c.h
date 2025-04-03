#ifndef IPC_C_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#define WORKERS_CONFIG 5
#define MAX_JOBS 500000

typedef struct {
    int worker_type;
    int worker_count;
} worker_congfig_t;

typedef struct {
    int worker_type;
    pid_t pid;
    int busy;
    int pipe_main_to_worker[2];
    int pipe_worker_to_main[2];
} worker_info_t;

typedef struct {
    int job_type;
    int job_duration;
} job_t;

job_t job_queue[MAX_JOBS];
int job_count = 0;

void
enqueue_job(job_t job) {
    if(job_count < MAX_JOBS) {
        job_queue[job_count++] = job;
    } else {
        fprintf(stderr, "Job queue full! Dropping job.\n");
    }
}

job_t
dequeue_job(int index) {
    job_t job = job_queue[index];
    for (int i = index; i < job_count - 1; i++) {
        job_queue[i] = job_queue[i + 1];
    }
    job_count--;
    return job;
}

#endif
