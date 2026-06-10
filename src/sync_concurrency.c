#define _WIN32_WINNT 0x0600
#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static CRITICAL_SECTION g_log_lock;

static void log_line(const char *message)
{
    EnterCriticalSection(&g_log_lock);
    printf("%s\n", message);
    LeaveCriticalSection(&g_log_lock);
}

typedef struct {
    int *buffer;
    int buffer_size;
    int head;
    int tail;
    int count;
    int produced;
    int consumed;
    int total_items;
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE not_full;
    CONDITION_VARIABLE not_empty;
} PCState;

typedef struct {
    PCState *state;
    int id;
    int items_per_producer;
} ProducerArg;

typedef struct {
    PCState *state;
    int id;
} ConsumerArg;

static DWORD WINAPI producer_thread(LPVOID data)
{
    ProducerArg *arg = (ProducerArg *)data;
    PCState *state = arg->state;
    char message[256];

    for (int i = 1; i <= arg->items_per_producer; i++) {
        int item;
        int count_after;
        int all_produced;
        Sleep(40);

        EnterCriticalSection(&state->lock);
        while (state->count >= state->buffer_size) {
            SleepConditionVariableCS(&state->not_full, &state->lock, INFINITE);
        }
        item = arg->id * 100 + i;
        state->buffer[state->tail] = item;
        state->tail = (state->tail + 1) % state->buffer_size;
        state->count++;
        state->produced++;
        count_after = state->count;
        all_produced = state->produced == state->total_items;
        LeaveCriticalSection(&state->lock);

        snprintf(message, sizeof(message),
                 "生产者 P%d 生产 item=%d，缓冲区数量=%d",
                 arg->id, item, count_after);
        log_line(message);
        if (all_produced) {
            WakeAllConditionVariable(&state->not_empty);
        } else {
            WakeConditionVariable(&state->not_empty);
        }
    }
    return 0;
}

static DWORD WINAPI consumer_thread(LPVOID data)
{
    ConsumerArg *arg = (ConsumerArg *)data;
    PCState *state = arg->state;
    char message[256];

    while (1) {
        int item;
        int count_after;
        int all_consumed;

        EnterCriticalSection(&state->lock);
        while (state->count == 0 && state->produced < state->total_items) {
            SleepConditionVariableCS(&state->not_empty, &state->lock, INFINITE);
        }
        if (state->count == 0 && state->produced == state->total_items) {
            LeaveCriticalSection(&state->lock);
            break;
        }
        item = state->buffer[state->head];
        state->head = (state->head + 1) % state->buffer_size;
        state->count--;
        state->consumed++;
        count_after = state->count;
        all_consumed = state->consumed == state->total_items;
        LeaveCriticalSection(&state->lock);

        snprintf(message, sizeof(message),
                 "消费者 C%d 消费 item=%d，缓冲区数量=%d",
                 arg->id, item, count_after);
        log_line(message);
        WakeConditionVariable(&state->not_full);
        if (all_consumed) {
            WakeAllConditionVariable(&state->not_empty);
        }
        Sleep(60);
    }
    return 0;
}

static void producer_consumer(void)
{
    int producer_count, consumer_count, buffer_size, items_per_producer;
    HANDLE *threads;
    ProducerArg *producer_args;
    ConsumerArg *consumer_args;
    PCState state;
    int total_threads;
    int thread_index = 0;

    printf("输入生产者数 消费者数 缓冲区大小 每个生产者生产数量: ");
    if (scanf("%d%d%d%d", &producer_count, &consumer_count, &buffer_size, &items_per_producer) != 4 ||
        producer_count <= 0 || consumer_count <= 0 || buffer_size <= 0 || items_per_producer <= 0) {
        printf("所有参数都必须大于 0。\n");
        return;
    }

    total_threads = producer_count + consumer_count;
    threads = (HANDLE *)calloc((size_t)total_threads, sizeof(HANDLE));
    producer_args = (ProducerArg *)calloc((size_t)producer_count, sizeof(ProducerArg));
    consumer_args = (ConsumerArg *)calloc((size_t)consumer_count, sizeof(ConsumerArg));
    state.buffer = (int *)calloc((size_t)buffer_size, sizeof(int));
    if (threads == NULL || producer_args == NULL || consumer_args == NULL || state.buffer == NULL) {
        printf("内存不足。\n");
        free(threads);
        free(producer_args);
        free(consumer_args);
        free(state.buffer);
        return;
    }

    state.buffer_size = buffer_size;
    state.head = state.tail = state.count = 0;
    state.produced = state.consumed = 0;
    state.total_items = producer_count * items_per_producer;
    InitializeCriticalSection(&state.lock);
    InitializeConditionVariable(&state.not_full);
    InitializeConditionVariable(&state.not_empty);

    for (int p = 1; p <= producer_count; p++) {
        producer_args[p - 1].state = &state;
        producer_args[p - 1].id = p;
        producer_args[p - 1].items_per_producer = items_per_producer;
        threads[thread_index++] = CreateThread(NULL, 0, producer_thread, &producer_args[p - 1], 0, NULL);
    }
    for (int c = 1; c <= consumer_count; c++) {
        consumer_args[c - 1].state = &state;
        consumer_args[c - 1].id = c;
        threads[thread_index++] = CreateThread(NULL, 0, consumer_thread, &consumer_args[c - 1], 0, NULL);
    }

    for (int i = 0; i < total_threads; i++) {
        if (threads[i] != NULL) {
            WaitForSingleObject(threads[i], INFINITE);
            CloseHandle(threads[i]);
        }
    }
    printf("生产者-消费者结束：共生产 %d 个，消费 %d 个。\n", state.produced, state.consumed);

    DeleteCriticalSection(&state.lock);
    free(state.buffer);
    free(threads);
    free(producer_args);
    free(consumer_args);
}

typedef struct {
    int active_readers;
    int waiting_writers;
    int writer_active;
    int shared_data;
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE cv;
} RWState;

typedef struct {
    RWState *state;
    int id;
} RWArg;

static DWORD WINAPI reader_thread(LPVOID data)
{
    RWArg *arg = (RWArg *)data;
    RWState *state = arg->state;
    char message[256];

    for (int round = 1; round <= 2; round++) {
        int active_readers;
        int value;

        EnterCriticalSection(&state->lock);
        while (state->writer_active || state->waiting_writers > 0) {
            SleepConditionVariableCS(&state->cv, &state->lock, INFINITE);
        }
        state->active_readers++;
        active_readers = state->active_readers;
        LeaveCriticalSection(&state->lock);

        snprintf(message, sizeof(message), "读者 R%d 开始读，第 %d 次，activeReaders=%d",
                 arg->id, round, active_readers);
        log_line(message);
        Sleep(50);

        EnterCriticalSection(&state->lock);
        value = state->shared_data;
        LeaveCriticalSection(&state->lock);
        snprintf(message, sizeof(message), "读者 R%d 读取 sharedData=%d", arg->id, value);
        log_line(message);

        EnterCriticalSection(&state->lock);
        state->active_readers--;
        active_readers = state->active_readers;
        snprintf(message, sizeof(message), "读者 R%d 结束读，activeReaders=%d",
                 arg->id, active_readers);
        if (state->active_readers == 0) {
            WakeAllConditionVariable(&state->cv);
        }
        LeaveCriticalSection(&state->lock);
        log_line(message);
        Sleep(40);
    }
    return 0;
}

static DWORD WINAPI writer_thread(LPVOID data)
{
    RWArg *arg = (RWArg *)data;
    RWState *state = arg->state;
    char message[256];

    for (int round = 1; round <= 2; round++) {
        int value;
        EnterCriticalSection(&state->lock);
        state->waiting_writers++;
        while (state->writer_active || state->active_readers > 0) {
            SleepConditionVariableCS(&state->cv, &state->lock, INFINITE);
        }
        state->waiting_writers--;
        state->writer_active = 1;
        LeaveCriticalSection(&state->lock);

        snprintf(message, sizeof(message), "写者 W%d 开始写，第 %d 次", arg->id, round);
        log_line(message);
        Sleep(80);

        EnterCriticalSection(&state->lock);
        state->shared_data++;
        value = state->shared_data;
        LeaveCriticalSection(&state->lock);
        snprintf(message, sizeof(message), "写者 W%d 写入 sharedData=%d", arg->id, value);
        log_line(message);

        EnterCriticalSection(&state->lock);
        state->writer_active = 0;
        WakeAllConditionVariable(&state->cv);
        LeaveCriticalSection(&state->lock);
        snprintf(message, sizeof(message), "写者 W%d 结束写", arg->id);
        log_line(message);
        Sleep(70);
    }
    return 0;
}

static void readers_writers(void)
{
    int reader_count, writer_count;
    int total_threads;
    HANDLE *threads;
    RWArg *reader_args;
    RWArg *writer_args;
    RWState state;
    int thread_index = 0;

    printf("输入读者数 写者数: ");
    if (scanf("%d%d", &reader_count, &writer_count) != 2 ||
        reader_count <= 0 || writer_count <= 0) {
        printf("读者数和写者数必须大于 0。\n");
        return;
    }

    total_threads = reader_count + writer_count;
    threads = (HANDLE *)calloc((size_t)total_threads, sizeof(HANDLE));
    reader_args = (RWArg *)calloc((size_t)reader_count, sizeof(RWArg));
    writer_args = (RWArg *)calloc((size_t)writer_count, sizeof(RWArg));
    if (threads == NULL || reader_args == NULL || writer_args == NULL) {
        printf("内存不足。\n");
        free(threads);
        free(reader_args);
        free(writer_args);
        return;
    }

    state.active_readers = 0;
    state.waiting_writers = 0;
    state.writer_active = 0;
    state.shared_data = 0;
    InitializeCriticalSection(&state.lock);
    InitializeConditionVariable(&state.cv);

    for (int r = 1; r <= reader_count; r++) {
        reader_args[r - 1].state = &state;
        reader_args[r - 1].id = r;
        threads[thread_index++] = CreateThread(NULL, 0, reader_thread, &reader_args[r - 1], 0, NULL);
    }
    for (int w = 1; w <= writer_count; w++) {
        writer_args[w - 1].state = &state;
        writer_args[w - 1].id = w;
        threads[thread_index++] = CreateThread(NULL, 0, writer_thread, &writer_args[w - 1], 0, NULL);
    }

    for (int i = 0; i < total_threads; i++) {
        if (threads[i] != NULL) {
            WaitForSingleObject(threads[i], INFINITE);
            CloseHandle(threads[i]);
        }
    }
    printf("读者-写者结束：最终 sharedData=%d。\n", state.shared_data);

    DeleteCriticalSection(&state.lock);
    free(threads);
    free(reader_args);
    free(writer_args);
}

typedef struct {
    CRITICAL_SECTION *forks;
    int philosopher_count;
    int rounds;
    int id;
} PhilosopherArg;

static DWORD WINAPI philosopher_thread(LPVOID data)
{
    PhilosopherArg *arg = (PhilosopherArg *)data;
    char message[256];
    int left = arg->id;
    int right = (arg->id + 1) % arg->philosopher_count;
    int first = left < right ? left : right;
    int second = left < right ? right : left;

    for (int round = 1; round <= arg->rounds; round++) {
        snprintf(message, sizeof(message), "哲学家 %d 思考，第 %d 轮", arg->id, round);
        log_line(message);
        Sleep(50);

        EnterCriticalSection(&arg->forks[first]);
        EnterCriticalSection(&arg->forks[second]);
        snprintf(message, sizeof(message),
                 "哲学家 %d 同时拿到叉子 %d 和 %d，开始进餐",
                 arg->id, left, right);
        log_line(message);
        Sleep(70);
        snprintf(message, sizeof(message), "哲学家 %d 放下叉子，结束进餐", arg->id);
        log_line(message);
        LeaveCriticalSection(&arg->forks[second]);
        LeaveCriticalSection(&arg->forks[first]);
    }
    return 0;
}

static void dining_philosophers(void)
{
    int philosopher_count, rounds;
    CRITICAL_SECTION *forks;
    HANDLE *threads;
    PhilosopherArg *args;

    printf("输入哲学家数量 进餐轮数: ");
    if (scanf("%d%d", &philosopher_count, &rounds) != 2 ||
        philosopher_count < 2 || rounds <= 0) {
        printf("哲学家数量至少为 2，进餐轮数必须大于 0。\n");
        return;
    }

    forks = (CRITICAL_SECTION *)calloc((size_t)philosopher_count, sizeof(CRITICAL_SECTION));
    threads = (HANDLE *)calloc((size_t)philosopher_count, sizeof(HANDLE));
    args = (PhilosopherArg *)calloc((size_t)philosopher_count, sizeof(PhilosopherArg));
    if (forks == NULL || threads == NULL || args == NULL) {
        printf("内存不足。\n");
        free(forks);
        free(threads);
        free(args);
        return;
    }

    for (int i = 0; i < philosopher_count; i++) {
        InitializeCriticalSection(&forks[i]);
    }
    for (int i = 0; i < philosopher_count; i++) {
        args[i].forks = forks;
        args[i].philosopher_count = philosopher_count;
        args[i].rounds = rounds;
        args[i].id = i;
        threads[i] = CreateThread(NULL, 0, philosopher_thread, &args[i], 0, NULL);
    }
    for (int i = 0; i < philosopher_count; i++) {
        if (threads[i] != NULL) {
            WaitForSingleObject(threads[i], INFINITE);
            CloseHandle(threads[i]);
        }
    }
    printf("哲学家进餐结束：所有哲学家均完成进餐，未发生死锁。\n");

    for (int i = 0; i < philosopher_count; i++) {
        DeleteCriticalSection(&forks[i]);
    }
    free(forks);
    free(threads);
    free(args);
}

int main(void)
{
    InitializeCriticalSection(&g_log_lock);
    while (1) {
        int choice;
        printf("\n进程同步与并发控制实验\n");
        printf("1. 生产者-消费者\n");
        printf("2. 读者-写者\n");
        printf("3. 哲学家进餐\n");
        printf("0. 退出\n");
        printf("请输入选项: ");

        if (scanf("%d", &choice) != 1) {
            break;
        }
        if (choice == 0) {
            break;
        } else if (choice == 1) {
            producer_consumer();
        } else if (choice == 2) {
            readers_writers();
        } else if (choice == 3) {
            dining_philosophers();
        } else {
            printf("无效选项。\n");
        }
    }
    DeleteCriticalSection(&g_log_lock);
    return 0;
}
