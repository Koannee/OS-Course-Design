#define _WIN32_WINNT 0x0600
#define WIN32_LEAN_AND_MEAN
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_THREADS 64

typedef unsigned long long U64;

typedef struct {
    size_t item_count;
    int thread_count;
    int bucket_count;
    int hot_percent;
    int hot_buckets;
    int heavy_percent;
    int heavy_cost;
    int chunk_size;
} Config;

typedef struct {
    uint32_t *keys;
    uint16_t *costs;
    size_t item_count;
    int bucket_count;
} Workload;

typedef struct {
    char name[64];
    double milliseconds;
    double throughput;
    double speedup;
    U64 checksum;
    int correct;
} BenchResult;

typedef struct {
    const Workload *workload;
    int thread_index;
    int thread_count;
    U64 *histogram;
    CRITICAL_SECTION *histogram_lock;
    CRITICAL_SECTION *bucket_locks;
    volatile LONG64 *atomic_counters;
    U64 *locals;
    volatile LONG64 *next_index;
    int chunk_size;
} WorkerArg;

typedef U64 *(*HistogramFunc)(const Workload *workload, int thread_count, int chunk_size);

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

static Config default_config(void)
{
    SYSTEM_INFO info;
    Config cfg;
    GetSystemInfo(&info);
    cfg.item_count = 600000;
    cfg.thread_count = (int)info.dwNumberOfProcessors;
    if (cfg.thread_count < 2) {
        cfg.thread_count = 2;
    }
    if (cfg.thread_count > MAX_THREADS) {
        cfg.thread_count = MAX_THREADS;
    }
    cfg.bucket_count = 4096;
    cfg.hot_percent = 70;
    cfg.hot_buckets = 16;
    cfg.heavy_percent = 20;
    cfg.heavy_cost = 40;
    cfg.chunk_size = 4096;
    return cfg;
}

static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static uint32_t rng_next(uint32_t *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}

static int rng_range(uint32_t *state, int max_value)
{
    if (max_value <= 0) {
        return 0;
    }
    return (int)(rng_next(state) % (uint32_t)max_value);
}

static uint32_t compute_bucket(uint32_t key, uint16_t cost, int bucket_count)
{
    uint32_t value = key;
    for (uint16_t i = 0; i < cost; i++) {
        value = mix32(value + 0x9e3779b9U + i);
    }
    return value % (uint32_t)bucket_count;
}

static double now_ms(void)
{
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
}

static U64 *alloc_histogram(int bucket_count)
{
    return (U64 *)calloc((size_t)bucket_count, sizeof(U64));
}

static U64 checksum_of(const U64 *histogram, int bucket_count)
{
    U64 sum = 0;
    for (int i = 0; i < bucket_count; i++) {
        sum += histogram[i];
    }
    return sum;
}

static int same_histogram(const U64 *a, const U64 *b, int bucket_count)
{
    for (int i = 0; i < bucket_count; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static void free_workload(Workload *workload)
{
    free(workload->keys);
    free(workload->costs);
    workload->keys = NULL;
    workload->costs = NULL;
}

static Workload make_contention_workload(const Config *cfg)
{
    Workload workload;
    uint32_t rng = 20260608U;
    workload.item_count = cfg->item_count;
    workload.bucket_count = cfg->bucket_count;
    workload.keys = (uint32_t *)malloc(sizeof(uint32_t) * cfg->item_count);
    workload.costs = (uint16_t *)calloc(cfg->item_count, sizeof(uint16_t));
    if (workload.keys == NULL || workload.costs == NULL) {
        fprintf(stderr, "内存不足。\n");
        exit(1);
    }
    for (size_t i = 0; i < cfg->item_count; i++) {
        if (rng_range(&rng, 100) < cfg->hot_percent) {
            workload.keys[i] = (uint32_t)rng_range(&rng, cfg->hot_buckets);
        } else {
            workload.keys[i] = (uint32_t)rng_range(&rng, cfg->bucket_count);
        }
    }
    return workload;
}

static Workload make_imbalanced_workload(const Config *cfg)
{
    Workload workload;
    uint32_t rng = 20260609U;
    size_t heavy_end = cfg->item_count * (size_t)cfg->heavy_percent / 100;
    workload.item_count = cfg->item_count;
    workload.bucket_count = cfg->bucket_count;
    workload.keys = (uint32_t *)malloc(sizeof(uint32_t) * cfg->item_count);
    workload.costs = (uint16_t *)malloc(sizeof(uint16_t) * cfg->item_count);
    if (workload.keys == NULL || workload.costs == NULL) {
        fprintf(stderr, "内存不足。\n");
        exit(1);
    }
    for (size_t i = 0; i < cfg->item_count; i++) {
        workload.keys[i] = (uint32_t)rng_range(&rng, cfg->bucket_count);
        workload.costs[i] = (uint16_t)(i < heavy_end ? cfg->heavy_cost : 1);
    }
    return workload;
}

static U64 *serial_histogram(const Workload *workload, int thread_count, int chunk_size)
{
    U64 *histogram = alloc_histogram(workload->bucket_count);
    (void)thread_count;
    (void)chunk_size;
    if (histogram == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < workload->item_count; i++) {
        uint32_t bucket = compute_bucket(workload->keys[i], workload->costs[i], workload->bucket_count);
        histogram[bucket]++;
    }
    return histogram;
}

static void start_and_wait(HANDLE handles[], WorkerArg args[], int thread_count,
                           DWORD (WINAPI *worker)(LPVOID))
{
    for (int t = 0; t < thread_count; t++) {
        handles[t] = CreateThread(NULL, 0, worker, &args[t], 0, NULL);
    }
    for (int t = 0; t < thread_count; t++) {
        if (handles[t] != NULL) {
            WaitForSingleObject(handles[t], INFINITE);
            CloseHandle(handles[t]);
        }
    }
}

static DWORD WINAPI coarse_worker(LPVOID data)
{
    WorkerArg *arg = (WorkerArg *)data;
    size_t begin = arg->workload->item_count * (size_t)arg->thread_index / (size_t)arg->thread_count;
    size_t end = arg->workload->item_count * (size_t)(arg->thread_index + 1) / (size_t)arg->thread_count;
    for (size_t i = begin; i < end; i++) {
        uint32_t bucket = compute_bucket(arg->workload->keys[i], arg->workload->costs[i],
                                         arg->workload->bucket_count);
        EnterCriticalSection(arg->histogram_lock);
        arg->histogram[bucket]++;
        LeaveCriticalSection(arg->histogram_lock);
    }
    return 0;
}

static U64 *coarse_mutex_histogram(const Workload *workload, int thread_count, int chunk_size)
{
    U64 *histogram = alloc_histogram(workload->bucket_count);
    HANDLE handles[MAX_THREADS];
    WorkerArg args[MAX_THREADS];
    CRITICAL_SECTION lock;
    (void)chunk_size;
    if (histogram == NULL) {
        return NULL;
    }
    InitializeCriticalSection(&lock);
    for (int t = 0; t < thread_count; t++) {
        memset(&args[t], 0, sizeof(args[t]));
        args[t].workload = workload;
        args[t].thread_index = t;
        args[t].thread_count = thread_count;
        args[t].histogram = histogram;
        args[t].histogram_lock = &lock;
    }
    start_and_wait(handles, args, thread_count, coarse_worker);
    DeleteCriticalSection(&lock);
    return histogram;
}

static DWORD WINAPI bucket_lock_worker(LPVOID data)
{
    WorkerArg *arg = (WorkerArg *)data;
    size_t begin = arg->workload->item_count * (size_t)arg->thread_index / (size_t)arg->thread_count;
    size_t end = arg->workload->item_count * (size_t)(arg->thread_index + 1) / (size_t)arg->thread_count;
    for (size_t i = begin; i < end; i++) {
        uint32_t bucket = compute_bucket(arg->workload->keys[i], arg->workload->costs[i],
                                         arg->workload->bucket_count);
        EnterCriticalSection(&arg->bucket_locks[bucket]);
        arg->histogram[bucket]++;
        LeaveCriticalSection(&arg->bucket_locks[bucket]);
    }
    return 0;
}

static U64 *bucket_lock_histogram(const Workload *workload, int thread_count, int chunk_size)
{
    U64 *histogram = alloc_histogram(workload->bucket_count);
    CRITICAL_SECTION *locks = (CRITICAL_SECTION *)malloc(sizeof(CRITICAL_SECTION) * (size_t)workload->bucket_count);
    HANDLE handles[MAX_THREADS];
    WorkerArg args[MAX_THREADS];
    (void)chunk_size;
    if (histogram == NULL || locks == NULL) {
        free(histogram);
        free(locks);
        return NULL;
    }
    for (int i = 0; i < workload->bucket_count; i++) {
        InitializeCriticalSection(&locks[i]);
    }
    for (int t = 0; t < thread_count; t++) {
        memset(&args[t], 0, sizeof(args[t]));
        args[t].workload = workload;
        args[t].thread_index = t;
        args[t].thread_count = thread_count;
        args[t].histogram = histogram;
        args[t].bucket_locks = locks;
    }
    start_and_wait(handles, args, thread_count, bucket_lock_worker);
    for (int i = 0; i < workload->bucket_count; i++) {
        DeleteCriticalSection(&locks[i]);
    }
    free(locks);
    return histogram;
}

static DWORD WINAPI atomic_worker(LPVOID data)
{
    WorkerArg *arg = (WorkerArg *)data;
    size_t begin = arg->workload->item_count * (size_t)arg->thread_index / (size_t)arg->thread_count;
    size_t end = arg->workload->item_count * (size_t)(arg->thread_index + 1) / (size_t)arg->thread_count;
    for (size_t i = begin; i < end; i++) {
        uint32_t bucket = compute_bucket(arg->workload->keys[i], arg->workload->costs[i],
                                         arg->workload->bucket_count);
        InterlockedIncrement64(&arg->atomic_counters[bucket]);
    }
    return 0;
}

static U64 *atomic_histogram(const Workload *workload, int thread_count, int chunk_size)
{
    volatile LONG64 *counters = (volatile LONG64 *)calloc((size_t)workload->bucket_count, sizeof(LONG64));
    U64 *histogram = alloc_histogram(workload->bucket_count);
    HANDLE handles[MAX_THREADS];
    WorkerArg args[MAX_THREADS];
    (void)chunk_size;
    if (counters == NULL || histogram == NULL) {
        free((void *)counters);
        free(histogram);
        return NULL;
    }
    for (int t = 0; t < thread_count; t++) {
        memset(&args[t], 0, sizeof(args[t]));
        args[t].workload = workload;
        args[t].thread_index = t;
        args[t].thread_count = thread_count;
        args[t].atomic_counters = counters;
    }
    start_and_wait(handles, args, thread_count, atomic_worker);
    for (int i = 0; i < workload->bucket_count; i++) {
        histogram[i] = (U64)counters[i];
    }
    free((void *)counters);
    return histogram;
}

static DWORD WINAPI local_static_worker(LPVOID data)
{
    WorkerArg *arg = (WorkerArg *)data;
    U64 *local = arg->locals + (size_t)arg->thread_index * (size_t)arg->workload->bucket_count;
    size_t begin = arg->workload->item_count * (size_t)arg->thread_index / (size_t)arg->thread_count;
    size_t end = arg->workload->item_count * (size_t)(arg->thread_index + 1) / (size_t)arg->thread_count;
    for (size_t i = begin; i < end; i++) {
        uint32_t bucket = compute_bucket(arg->workload->keys[i], arg->workload->costs[i],
                                         arg->workload->bucket_count);
        local[bucket]++;
    }
    return 0;
}

static U64 *local_static_histogram(const Workload *workload, int thread_count, int chunk_size)
{
    U64 *locals = (U64 *)calloc((size_t)thread_count * (size_t)workload->bucket_count, sizeof(U64));
    U64 *histogram = alloc_histogram(workload->bucket_count);
    HANDLE handles[MAX_THREADS];
    WorkerArg args[MAX_THREADS];
    (void)chunk_size;
    if (locals == NULL || histogram == NULL) {
        free(locals);
        free(histogram);
        return NULL;
    }
    for (int t = 0; t < thread_count; t++) {
        memset(&args[t], 0, sizeof(args[t]));
        args[t].workload = workload;
        args[t].thread_index = t;
        args[t].thread_count = thread_count;
        args[t].locals = locals;
    }
    start_and_wait(handles, args, thread_count, local_static_worker);
    for (int t = 0; t < thread_count; t++) {
        U64 *local = locals + (size_t)t * (size_t)workload->bucket_count;
        for (int i = 0; i < workload->bucket_count; i++) {
            histogram[i] += local[i];
        }
    }
    free(locals);
    return histogram;
}

static DWORD WINAPI local_dynamic_worker(LPVOID data)
{
    WorkerArg *arg = (WorkerArg *)data;
    U64 *local = arg->locals + (size_t)arg->thread_index * (size_t)arg->workload->bucket_count;
    LONG64 chunk = (LONG64)max_int(1, arg->chunk_size);
    while (1) {
        LONG64 begin = InterlockedExchangeAdd64(arg->next_index, chunk);
        LONG64 end = begin + chunk;
        if ((size_t)begin >= arg->workload->item_count) {
            break;
        }
        if ((size_t)end > arg->workload->item_count) {
            end = (LONG64)arg->workload->item_count;
        }
        for (LONG64 i = begin; i < end; i++) {
            uint32_t bucket = compute_bucket(arg->workload->keys[i], arg->workload->costs[i],
                                             arg->workload->bucket_count);
            local[bucket]++;
        }
    }
    return 0;
}

static U64 *local_dynamic_histogram(const Workload *workload, int thread_count, int chunk_size)
{
    U64 *locals = (U64 *)calloc((size_t)thread_count * (size_t)workload->bucket_count, sizeof(U64));
    U64 *histogram = alloc_histogram(workload->bucket_count);
    volatile LONG64 next_index = 0;
    HANDLE handles[MAX_THREADS];
    WorkerArg args[MAX_THREADS];
    if (locals == NULL || histogram == NULL) {
        free(locals);
        free(histogram);
        return NULL;
    }
    for (int t = 0; t < thread_count; t++) {
        memset(&args[t], 0, sizeof(args[t]));
        args[t].workload = workload;
        args[t].thread_index = t;
        args[t].thread_count = thread_count;
        args[t].locals = locals;
        args[t].next_index = &next_index;
        args[t].chunk_size = chunk_size;
    }
    start_and_wait(handles, args, thread_count, local_dynamic_worker);
    for (int t = 0; t < thread_count; t++) {
        U64 *local = locals + (size_t)t * (size_t)workload->bucket_count;
        for (int i = 0; i < workload->bucket_count; i++) {
            histogram[i] += local[i];
        }
    }
    free(locals);
    return histogram;
}

static BenchResult measure(const char *name, const Workload *workload, const U64 *reference,
                           double base_ms, HistogramFunc func, int thread_count, int chunk_size)
{
    double start = now_ms();
    U64 *histogram = func(workload, thread_count, chunk_size);
    double finish = now_ms();
    BenchResult result;
    memset(&result, 0, sizeof(result));
    snprintf(result.name, sizeof(result.name), "%s", name);
    result.milliseconds = finish - start;
    result.throughput = (double)workload->item_count / result.milliseconds / 1000.0;
    result.speedup = base_ms / result.milliseconds;
    result.checksum = histogram == NULL ? 0 : checksum_of(histogram, workload->bucket_count);
    result.correct = histogram != NULL && same_histogram(histogram, reference, workload->bucket_count);
    free(histogram);
    return result;
}

static void print_results(BenchResult results[], int count)
{
    printf("%-24s%14s%18s%12s%14s\n",
           "方案", "耗时(ms)", "吞吐(Mops/s)", "加速比", "校验");
    for (int i = 0; i < 82; i++) {
        putchar('-');
    }
    putchar('\n');
    for (int i = 0; i < count; i++) {
        printf("%-24s%14.3f%18.3f%12.3f%14s\n",
               results[i].name,
               results[i].milliseconds,
               results[i].throughput,
               results[i].speedup,
               results[i].correct ? "OK" : "ERROR");
    }
}

static void run_contention_experiment(const Config *cfg)
{
    Workload workload;
    U64 *reference;
    BenchResult results[8];
    int count = 0;
    double start, base_ms;

    printf("\n实验一：共享计数器并发优化（热点比例 %d%%）\n", cfg->hot_percent);
    workload = make_contention_workload(cfg);
    start = now_ms();
    reference = serial_histogram(&workload, 1, cfg->chunk_size);
    base_ms = now_ms() - start;

    snprintf(results[count].name, sizeof(results[count].name), "%s", "单线程基准");
    results[count].milliseconds = base_ms;
    results[count].throughput = (double)workload.item_count / base_ms / 1000.0;
    results[count].speedup = 1.0;
    results[count].checksum = checksum_of(reference, workload.bucket_count);
    results[count].correct = 1;
    count++;
    results[count++] = measure("粗粒度互斥锁", &workload, reference, base_ms,
                               coarse_mutex_histogram, cfg->thread_count, cfg->chunk_size);
    results[count++] = measure("桶级细粒度锁", &workload, reference, base_ms,
                               bucket_lock_histogram, cfg->thread_count, cfg->chunk_size);
    results[count++] = measure("原子计数器", &workload, reference, base_ms,
                               atomic_histogram, cfg->thread_count, cfg->chunk_size);
    results[count++] = measure("线程本地归约", &workload, reference, base_ms,
                               local_static_histogram, cfg->thread_count, cfg->chunk_size);
    print_results(results, count);

    free(reference);
    free_workload(&workload);
}

static void run_load_balance_experiment(const Config *cfg)
{
    Workload workload;
    U64 *reference;
    BenchResult results[4];
    int count = 0;
    double start, base_ms;

    printf("\n实验二：负载不均衡下的任务调度优化（前 %d%% 数据为重任务）\n",
           cfg->heavy_percent);
    workload = make_imbalanced_workload(cfg);
    start = now_ms();
    reference = serial_histogram(&workload, 1, cfg->chunk_size);
    base_ms = now_ms() - start;

    snprintf(results[count].name, sizeof(results[count].name), "%s", "单线程基准");
    results[count].milliseconds = base_ms;
    results[count].throughput = (double)workload.item_count / base_ms / 1000.0;
    results[count].speedup = 1.0;
    results[count].checksum = checksum_of(reference, workload.bucket_count);
    results[count].correct = 1;
    count++;
    results[count++] = measure("静态连续划分", &workload, reference, base_ms,
                               local_static_histogram, cfg->thread_count, cfg->chunk_size);
    results[count++] = measure("动态块调度", &workload, reference, base_ms,
                               local_dynamic_histogram, cfg->thread_count, cfg->chunk_size);
    print_results(results, count);

    free(reference);
    free_workload(&workload);
}

static void print_config(const Config *cfg)
{
    printf("\n当前参数：数据量=%I64u，线程数=%d，桶数=%d，热点比例=%d%%，"
           "重任务比例=%d%%，动态块大小=%d\n",
           (unsigned __int64)cfg->item_count,
           cfg->thread_count,
           cfg->bucket_count,
           cfg->hot_percent,
           cfg->heavy_percent,
           cfg->chunk_size);
}

static Config read_custom_config(void)
{
    Config cfg = default_config();
    unsigned __int64 item_count;
    printf("输入数据量 线程数 桶数: ");
    scanf("%I64u%d%d", &item_count, &cfg.thread_count, &cfg.bucket_count);
    cfg.item_count = (size_t)item_count;
    printf("输入热点比例(0-100) 热点桶数: ");
    scanf("%d%d", &cfg.hot_percent, &cfg.hot_buckets);
    printf("输入重任务比例(0-100) 重任务计算成本 动态块大小: ");
    scanf("%d%d%d", &cfg.heavy_percent, &cfg.heavy_cost, &cfg.chunk_size);

    cfg.thread_count = min_int(MAX_THREADS, max_int(1, cfg.thread_count));
    cfg.bucket_count = max_int(1, cfg.bucket_count);
    cfg.hot_percent = min_int(100, max_int(0, cfg.hot_percent));
    cfg.hot_buckets = min_int(cfg.bucket_count, max_int(1, cfg.hot_buckets));
    cfg.heavy_percent = min_int(100, max_int(0, cfg.heavy_percent));
    cfg.heavy_cost = max_int(1, cfg.heavy_cost);
    cfg.chunk_size = max_int(1, cfg.chunk_size);
    return cfg;
}

int main(void)
{
    Config cfg = default_config();
    printf("自由扩展提升：并发性能优化实验\n");
    printf("本程序对比锁粒度、原子操作、线程本地归约和动态任务调度的性能差异。\n");

    while (1) {
        int choice;
        printf("\n1. 使用默认参数运行\n");
        printf("2. 自定义参数运行\n");
        printf("0. 退出\n");
        printf("请输入选项: ");
        if (scanf("%d", &choice) != 1) {
            return 0;
        }
        if (choice == 0) {
            break;
        }
        if (choice == 1) {
            cfg = default_config();
        } else if (choice == 2) {
            cfg = read_custom_config();
        } else {
            printf("无效选项。\n");
            continue;
        }

        print_config(&cfg);
        run_contention_experiment(&cfg);
        run_load_balance_experiment(&cfg);
    }

    return 0;
}
