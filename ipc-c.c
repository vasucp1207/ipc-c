#include "ipc-c.h"

void
worker(int type, int read_fd, int write_fd) {
    int duration;
    size_t n;

    // Here worker is waiting for job from dispatcher
    while((n = read(read_fd, &duration, sizeof(duration))) > 0) {
        printf("Worker of type %d and PID %d is received job with duration %ds.\n", type, getpid(), duration);
        sleep(duration);
        char msg[100];
        snprintf(msg, sizeof(msg), "Worker of type %d and PID %d finished job of %ds.", type, getpid(), duration);
        if (write(write_fd, msg, sizeof(msg)) < 0) {
            fprintf(stderr, "Worker write failed");
        }
    }
    exit(EXIT_FAILURE);
}

int
main() {
    worker_congfig_t configs[WORKERS_CONFIG];
    int total_workers = 0;
    printf("Enter worker configuration (%d lines: <worker_type> <worker_count>):\n", WORKERS_CONFIG);
    for(int i = 0; i < WORKERS_CONFIG; i++) {
        if (scanf("%d %d", &configs[i].worker_type, &configs[i].worker_count) != 2) {
            fprintf(stderr, "Got wrong number of args, expected only two args\n");
            exit(EXIT_FAILURE);
        }
        if(configs[i].worker_type < 0 || configs[i].worker_type > 5) {
            fprintf(stderr, "Worker type should be between 0 and 5\n");
            exit(EXIT_FAILURE);
        }
        total_workers += configs[i].worker_count;
    }

    worker_info_t workers[total_workers];
    int worker_index = 0;
    for(int i = 0; i < WORKERS_CONFIG; i++) {
        for(int j = 0; j < configs[i].worker_count; j++) {
            // setting up unidirectional main to worker pipe
            if (pipe(workers[worker_index].pipe_main_to_worker) < 0) {
                fprintf(stderr, "Pipe main to worker failed");
                exit(EXIT_FAILURE);
            }
            // setting up unidirectional worker to main pipe
            if (pipe(workers[worker_index].pipe_worker_to_main) < 0) {
                fprintf(stderr, "Pipe worker to main failed");
                exit(EXIT_FAILURE);
            }
            pid_t pid = fork();
            if (pid < 0) {
                fprintf(stderr, "Forking a child failed");
                exit(EXIT_FAILURE);
            } else if (pid == 0) {
                close(workers[worker_index].pipe_main_to_worker[1]);
                close(workers[worker_index].pipe_worker_to_main[0]);
                worker(configs[i].worker_type, workers[worker_index].pipe_main_to_worker[0], workers[worker_index].pipe_worker_to_main[1]);
            } else {
                workers[worker_index].worker_type = configs[i].worker_type;
                workers[worker_index].pid = pid;
                // all workers are free initially
                workers[worker_index].busy = 0;
                close(workers[worker_index].pipe_main_to_worker[0]);
                close(workers[worker_index].pipe_worker_to_main[1]);
                worker_index++;
            }
        }
    }

    printf("Enter jobs (format: <job_type> <job_duration>, EOF to end):\n");

    // infinite loop, here select syscall is continously monitoring for jobs input through STDIN_FILENO,
    // and if any worker sends data to main through read_fd of worker to main
    while(1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        int max_fd = STDIN_FILENO;
        // we will monitor for all workers
        for(int i = 0; i < total_workers; i++) {
            FD_SET(workers[i].pipe_worker_to_main[0], &readfds);
            if (workers[i].pipe_worker_to_main[0] > max_fd) {
                max_fd = workers[i].pipe_worker_to_main[0];
            }
        }

        int ret = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (ret < 0) {
            fprintf(stderr, "select syscall failed");
            exit(EXIT_FAILURE);
        }

        // starts job parsing
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            job_t new_job;
            if (scanf("%d %d", &new_job.job_type, &new_job.job_duration) != 2) {
                fprintf(stderr, "Got wrong number of args, expected only two args\n");
                exit(EXIT_FAILURE);
            }

            if(new_job.job_type < 0 || new_job.job_type > 5) {
                fprintf(stderr, "Job type should be between 0 and 5\n");
                exit(EXIT_FAILURE);
            }

            // small delay to simulate job parsing without it we may gets race condition between stdin and main-worker pipes,
            // this is a hack may be we can use a better way than this
            sleep(1);

            int assigned = 0;
            // go through all the workers and see if any of them is free(for a particular type) and sends job data to it
            for (int i = 0; i < total_workers; i++) {
                if (workers[i].worker_type == new_job.job_type && workers[i].busy == 0) {
                    if (write(workers[i].pipe_main_to_worker[1], &new_job.job_duration, sizeof(int)) < 0) {
                        fprintf(stderr, "Write to worker failed");
                    } else {
                        printf("Dispatcher sent job of type %d and duration %d to worker PID %d\n", new_job.job_type, new_job.job_duration, workers[i].pid);
                        // now this worker is busy for some duration
                        workers[i].busy = 1;
                    }
                    assigned = 1;
                    break;
                }
            }
            // no worker is free for this job type so put this into the queue and process it when we have a free slot
            if (!assigned) {
                enqueue_job(new_job);
                printf("Dispatcher queued job of type %d and duration %d due to no free worker\n", new_job.job_type, new_job.job_duration);
            }
        }

        // continously monitors if any worker finished the job
        for (int i = 0; i < total_workers; i++) {
            // worker i finished the job
            if (FD_ISSET(workers[i].pipe_worker_to_main[0], &readfds)) {
                char response[100];
                int n = read(workers[i].pipe_worker_to_main[0], response, sizeof(response));
                if (n > 0) {
                    printf("Dispatcher received response from worker PID %d: %s\n", workers[i].pid, response);
                    workers[i].busy = 0;
                    // check if we have any job of same type of this worker,
                    // if any then dequeue the job and sends to this worker because its free now
                    for (int k = 0; k < job_count; k++) {
                        if (job_queue[k].job_type == workers[i].worker_type) {
                            int duration = job_queue[k].job_duration;
                            if (write(workers[i].pipe_main_to_worker[1], &duration, sizeof(int)) < 0) {
                                fprintf(stderr, "Write to worker failed");
                            } else {
                                printf("Dispatcher dequeued and sent job of type %d and duration %d to worker PID %d\n", workers[i].worker_type, duration, workers[i].pid);
                                // worker again busy
                                workers[i].busy = 1;
                                dequeue_job(k);
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < total_workers; i++) {
        close(workers[i].pipe_main_to_worker[1]);
        close(workers[i].pipe_worker_to_main[0]);
    }

    while (wait(NULL) > 0);

    printf("Exiting successfully\n");

    return 0;
}
