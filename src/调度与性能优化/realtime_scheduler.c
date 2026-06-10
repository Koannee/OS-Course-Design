#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_TASKS 16
#define MAX_JOBS 4096
#define MAX_TIME 10000
#define NAME_LEN 32

typedef enum {
    POLICY_RMS,
    POLICY_EDF
} Policy;

typedef struct {
    char name[NAME_LEN];
    int period;
    int execution;
    int deadline;
    int release_offset;
} Task;

typedef struct {
    int task_id;
    int job_no;
    int release_time;
    int absolute_deadline;
    int remaining_time;
    int finished_time;
    int missed_deadline;
} Job;

typedef struct {
    int jobs_released;
    int jobs_finished;
    int deadline_misses;
    int total_response_time;
    int max_response_time;
} TaskStat;

typedef struct {
    int total_jobs;
    int total_finished;
    int total_misses;
    int idle_time;
    int context_switches;
    double utilization;
} Summary;

static int gcd_int(int a, int b)
{
    while (b != 0) {
        int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static int lcm_int(int a, int b)
{
    return a / gcd_int(a, b) * b;
}

static int hyperperiod(Task tasks[], int task_count)
{
    int hp = tasks[0].period;
    for (int i = 1; i < task_count; i++) {
        hp = lcm_int(hp, tasks[i].period);
        if (hp > MAX_TIME) {
            return MAX_TIME;
        }
    }
    return hp;
}

static double taskset_utilization(Task tasks[], int task_count)
{
    double u = 0.0;
    for (int i = 0; i < task_count; i++) {
        u += (double)tasks[i].execution / tasks[i].period;
    }
    return u;
}

static int load_tasks_from_file(const char *path, Task tasks[], int *task_count)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    int count = 0;
    while (count < MAX_TASKS) {
        Task t;
        int fields = fscanf(fp, "%31s %d %d %d %d",
                            t.name, &t.period, &t.execution,
                            &t.deadline, &t.release_offset);
        if (fields == EOF) {
            break;
        }
        if (fields != 5) {
            fclose(fp);
            fprintf(stderr, "Input format error. Expected: name period execution deadline release_offset\n");
            return -1;
        }
        if (t.period <= 0 || t.execution <= 0 || t.deadline <= 0 ||
            t.release_offset < 0 || t.execution > t.deadline) {
            fclose(fp);
            fprintf(stderr, "Invalid task parameters near task %s.\n", t.name);
            return -1;
        }
        tasks[count++] = t;
    }
    fclose(fp);
    *task_count = count;
    return 1;
}

static void load_default_tasks(Task tasks[], int *task_count)
{
    Task defaults[] = {
        {"T1", 4, 1, 4, 0},
        {"T2", 5, 2, 5, 0},
        {"T3", 20, 5, 20, 0}
    };
    *task_count = (int)(sizeof(defaults) / sizeof(defaults[0]));
    for (int i = 0; i < *task_count; i++) {
        tasks[i] = defaults[i];
    }
}

static int release_jobs(Task tasks[], int task_count, Job jobs[], int *job_count,
                        TaskStat stats[], int now)
{
    for (int i = 0; i < task_count; i++) {
        if (now >= tasks[i].release_offset &&
            (now - tasks[i].release_offset) % tasks[i].period == 0) {
            if (*job_count >= MAX_JOBS) {
                fprintf(stderr, "Too many jobs. Increase MAX_JOBS.\n");
                return 0;
            }
            stats[i].jobs_released++;
            jobs[*job_count].task_id = i;
            jobs[*job_count].job_no = stats[i].jobs_released;
            jobs[*job_count].release_time = now;
            jobs[*job_count].absolute_deadline = now + tasks[i].deadline;
            jobs[*job_count].remaining_time = tasks[i].execution;
            jobs[*job_count].finished_time = -1;
            jobs[*job_count].missed_deadline = 0;
            (*job_count)++;
        }
    }
    return 1;
}

static int better_job(Policy policy, Task tasks[], Job jobs[], int a, int b)
{
    Job *ja = &jobs[a];
    Job *jb = &jobs[b];
    if (policy == POLICY_EDF) {
        if (ja->absolute_deadline != jb->absolute_deadline) {
            return ja->absolute_deadline < jb->absolute_deadline;
        }
    } else {
        int pa = tasks[ja->task_id].period;
        int pb = tasks[jb->task_id].period;
        if (pa != pb) {
            return pa < pb;
        }
    }
    if (ja->release_time != jb->release_time) {
        return ja->release_time < jb->release_time;
    }
    return ja->task_id < jb->task_id;
}

static int select_job(Policy policy, Task tasks[], Job jobs[], int job_count, int now)
{
    int best = -1;
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].release_time <= now && jobs[i].remaining_time > 0) {
            if (best == -1 || better_job(policy, tasks, jobs, i, best)) {
                best = i;
            }
        }
    }
    return best;
}

static void update_misses(Task tasks[], Job jobs[], int job_count, TaskStat stats[], int now)
{
    for (int i = 0; i < job_count; i++) {
        if (!jobs[i].missed_deadline && jobs[i].remaining_time > 0 &&
            now >= jobs[i].absolute_deadline) {
            jobs[i].missed_deadline = 1;
            stats[jobs[i].task_id].deadline_misses++;
        }
    }
    (void)tasks;
}

static void print_task_table(Task tasks[], int task_count)
{
    printf("Task set:\n");
    printf("+------+--------+-----------+----------+---------------+\n");
    printf("| Name | Period | Execution | Deadline | ReleaseOffset |\n");
    printf("+------+--------+-----------+----------+---------------+\n");
    for (int i = 0; i < task_count; i++) {
        printf("| %-4s | %6d | %9d | %8d | %13d |\n",
               tasks[i].name, tasks[i].period, tasks[i].execution,
               tasks[i].deadline, tasks[i].release_offset);
    }
    printf("+------+--------+-----------+----------+---------------+\n\n");
}

static void print_gantt(Task tasks[], int timeline[], int duration)
{
    printf("Gantt timeline, one slot means one time unit:\n");
    printf("time : ");
    for (int t = 0; t < duration; t++) {
        printf("%2d ", t);
    }
    printf("\nrun  : ");
    for (int t = 0; t < duration; t++) {
        if (timeline[t] < 0) {
            printf(" - ");
        } else {
            printf("%2s ", tasks[timeline[t]].name);
        }
    }
    printf("\n\n");
}

static Summary simulate(Task tasks[], int task_count, Policy policy, int duration, int verbose)
{
    Job jobs[MAX_JOBS];
    TaskStat stats[MAX_TASKS];
    int timeline[MAX_TIME];
    int job_count = 0;
    int previous_task = -2;
    Summary summary;
    memset(stats, 0, sizeof(stats));
    memset(&summary, 0, sizeof(summary));
    for (int i = 0; i < duration; i++) {
        timeline[i] = -1;
    }
    for (int now = 0; now < duration; now++) {
        if (!release_jobs(tasks, task_count, jobs, &job_count, stats, now)) {
            exit(1);
        }
        update_misses(tasks, jobs, job_count, stats, now);
        int selected = select_job(policy, tasks, jobs, job_count, now);
        if (selected == -1) {
            summary.idle_time++;
            previous_task = -1;
            continue;
        }
        int running_task = jobs[selected].task_id;
        timeline[now] = running_task;
        if (previous_task >= 0 && previous_task != running_task) {
            summary.context_switches++;
        }
        previous_task = running_task;
        jobs[selected].remaining_time--;
        if (jobs[selected].remaining_time == 0) {
            int response_time = now + 1 - jobs[selected].release_time;
            TaskStat *st = &stats[running_task];
            jobs[selected].finished_time = now + 1;
            st->jobs_finished++;
            st->total_response_time += response_time;
            if (response_time > st->max_response_time) {
                st->max_response_time = response_time;
            }
        }
    }
    update_misses(tasks, jobs, job_count, stats, duration);
    printf("Policy: %s\n", policy == POLICY_EDF ? "EDF (Earliest Deadline First)" :
                                            "RMS (Rate Monotonic Scheduling)");
    printf("Simulation duration: %d\n", duration);
    printf("CPU theoretical utilization: %.2f%%\n", taskset_utilization(tasks, task_count) * 100.0);
    printf("CPU measured busy ratio: %.2f%%\n\n", (double)(duration - summary.idle_time) * 100.0 / duration);
    printf("+------+----------+----------+--------+-------------+-------------+\n");
    printf("| Task | Released | Finished | Misses | AvgResponse | MaxResponse |\n");
    printf("+------+----------+----------+--------+-------------+-------------+\n");
    for (int i = 0; i < task_count; i++) {
        double avg = stats[i].jobs_finished == 0 ? 0.0 :
                     (double)stats[i].total_response_time / stats[i].jobs_finished;
        printf("| %-4s | %8d | %8d | %6d | %11.2f | %11d |\n",
               tasks[i].name, stats[i].jobs_released, stats[i].jobs_finished,
               stats[i].deadline_misses, avg, stats[i].max_response_time);

        summary.total_jobs += stats[i].jobs_released;
        summary.total_finished += stats[i].jobs_finished;
        summary.total_misses += stats[i].deadline_misses;
    }
    printf("+------+----------+----------+--------+-------------+-------------+\n");
    for (int i = 0; i < job_count; i++) {
        if (!jobs[i].missed_deadline && jobs[i].remaining_time > 0) {
            summary.total_misses++;
        }
    }
    summary.utilization = (double)(duration - summary.idle_time) / duration;
    printf("Idle time: %d\n", summary.idle_time);
    printf("Context switches: %d\n", summary.context_switches);
    printf("Total released jobs: %d\n", summary.total_jobs);
    printf("Total finished jobs: %d\n", summary.total_finished);
    printf("Total deadline misses: %d\n\n", summary.total_misses);
    if (verbose && duration <= 120) {
        print_gantt(tasks, timeline, duration);
    } else if (verbose) {
        printf("Gantt timeline skipped because duration > 120.\n\n");
    }
    return summary;
}

static void print_help(const char *program)
{
    printf("Usage:\n");
    printf("  %s [task_file] [duration]\n\n", program);
    printf("Task file format, one task per line:\n");
    printf("  name period execution deadline release_offset\n\n");
    printf("Example:\n");
    printf("  T1 4 1 4 0\n");
    printf("  T2 5 2 5 0\n");
    printf("  T3 20 5 20 0\n\n");
    printf("If no task file is given, the built-in example task set is used.\n");
}

int main(int argc, char *argv[])
{
    Task tasks[MAX_TASKS];
    int task_count = 0;
    int duration;
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_help(argv[0]);
        return 0;
    }
    if (argc > 1) {
        int loaded = load_tasks_from_file(argv[1], tasks, &task_count);
        if (loaded < 0) {
            return 1;
        }
        if (loaded == 0) {
            fprintf(stderr, "Cannot open task file: %s\n", argv[1]);
            return 1;
        }
    } else {
        load_default_tasks(tasks, &task_count);
    }
    if (task_count == 0) {
        fprintf(stderr, "No tasks loaded.\n");
        return 1;
    }
    duration = hyperperiod(tasks, task_count);
    if (argc > 2) {
        duration = atoi(argv[2]);
        if (duration <= 0 || duration > MAX_TIME) {
            fprintf(stderr, "Duration must be in range 1..%d.\n", MAX_TIME);
            return 1;
        }
    }
    print_task_table(tasks, task_count);
    printf("RMS sufficient schedulability bound for %d tasks: %.2f%%\n",
           task_count,
           task_count * (pow(2.0, 1.0 / task_count) - 1.0) * 100.0);
    printf("EDF schedulability bound for implicit-deadline periodic tasks: 100.00%%\n\n");
    simulate(tasks, task_count, POLICY_RMS, duration, 1);
    simulate(tasks, task_count, POLICY_EDF, duration, 1);
    return 0;
}
