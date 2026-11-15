/* tests/queue_mpsc.c — inbox 队列 MPSC 并发契约（PH15-T1）。
 *
 * 判据（docs/phases/PH-15/card.md §4/§5 E1、E2）：
 *   8 个生产者各 push 10^4 次，单消费者并发 pop；
 *   accepted + rejected == 80000；每条被接受的消息恰好出队一次。
 *   TSAN 档同一夹具重编重跑，stderr 不得出现 "WARNING: ThreadSanitizer"。
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "queue.h"

#define THREADS 8
#define PER_THREAD 10000
#define TOTAL (THREADS * PER_THREAD)
#define CAPACITY 4096
#define Q_EMPTY (-4)
/* 无锁实现的计数竞争会让 pop 的移位规模失控；看门狗把挂死转成可判定的失败。 */
#define WATCHDOG_SECONDS 30

static unsigned char seen[THREADS][PER_THREAD];
static unsigned char accepted_flag[THREADS][PER_THREAD];

static pthread_mutex_t done_lock = PTHREAD_MUTEX_INITIALIZER;
static int producers_done;

struct producer {
    void *q;
    int index;
    int accepted;
    int rejected;
    int errors;
};

static int parse_item(const char *s, int *thread, int *seq)
{
    if (sscanf(s, "%d:%d", thread, seq) != 2) return 0;
    return *thread >= 0 && *thread < THREADS
           && *seq >= 0 && *seq < PER_THREAD;
}

static void *produce(void *arg)
{
    struct producer *p = arg;
    char buf[32];
    int32_t len;
    int32_t rc;
    int i;

    for (i = 0; i < PER_THREAD; i++) {
        len = (int32_t)snprintf(buf, sizeof(buf), "%d:%d", p->index, i);
        rc = queue_push(p->q, buf, len);
        if (rc == 0) {
            accepted_flag[p->index][i] = 1;
            p->accepted = p->accepted + 1;
        } else if (rc == -2) {
            p->rejected = p->rejected + 1;
        } else {
            fprintf(stderr, "queue_mpsc: push 返回 %d（t=%d i=%d）\n",
                    (int)rc, p->index, i);
            p->errors = p->errors + 1;
        }
    }

    pthread_mutex_lock(&done_lock);
    producers_done = producers_done + 1;
    pthread_mutex_unlock(&done_lock);
    return 0;
}

static void record_item(char *data, int *popped, int *bad)
{
    int thread;
    int seq;

    if (!parse_item(data, &thread, &seq)) {
        fprintf(stderr, "queue_mpsc: 非法负载 %s\n", data);
        *bad = *bad + 1;
    } else if (seen[thread][seq] != 0) {
        fprintf(stderr, "queue_mpsc: 重复出队 %s\n", data);
        *bad = *bad + 1;
    } else {
        seen[thread][seq] = 1;
    }
    *popped = *popped + 1;
}

/* 排空队列；返回 0 表示队列已空，1 表示遇到非空返回值错误。 */
static int drain_queue(void *q, int *popped, int *bad)
{
    char *data;
    int32_t len;
    int32_t rc;

    for (;;) {
        data = 0;
        len = 0;
        rc = queue_pop(q, &data, &len);
        if (rc == 0) {
            record_item(data, popped, bad);
            free(data);
            continue;
        }
        if (rc == Q_EMPTY) return 0;
        fprintf(stderr, "queue_mpsc: pop 返回 %d\n", (int)rc);
        return 1;
    }
}

static void *watchdog(void *arg)
{
    (void)arg;
    sleep(WATCHDOG_SECONDS);
    fprintf(stderr, "queue_mpsc: 超时（%d s 内未完成 MPSC 压测）\n",
            WATCHDOG_SECONDS);
    _exit(12);
    return 0;
}

int main(void)
{
    pthread_t threads[THREADS];
    pthread_t dog;
    struct producer prod[THREADS];
    void *q;
    int popped;
    int bad;
    int accepted;
    int rejected;
    int errors;
    int done;
    int i;
    int t;

    q = queue_open(CAPACITY);
    if (q == 0) {
        fprintf(stderr, "queue_mpsc: queue_open 失败\n");
        return 1;
    }
    if (pthread_create(&dog, 0, watchdog, 0) != 0) {
        fprintf(stderr, "queue_mpsc: watchdog 创建失败\n");
        return 1;
    }
    pthread_detach(dog);

    for (i = 0; i < THREADS; i++) {
        prod[i].q = q;
        prod[i].index = i;
        prod[i].accepted = 0;
        prod[i].rejected = 0;
        prod[i].errors = 0;
        if (pthread_create(&threads[i], 0, produce, &prod[i]) != 0) {
            fprintf(stderr, "queue_mpsc: pthread_create 失败（t=%d）\n", i);
            return 2;
        }
    }

    popped = 0;
    bad = 0;
    for (;;) {
        if (drain_queue(q, &popped, &bad) != 0) return 3;
        pthread_mutex_lock(&done_lock);
        done = producers_done;
        pthread_mutex_unlock(&done_lock);
        if (done == THREADS) break;
        usleep(1000);
    }

    for (i = 0; i < THREADS; i++) {
        if (pthread_join(threads[i], 0) != 0) {
            fprintf(stderr, "queue_mpsc: pthread_join 失败（t=%d）\n", i);
            return 4;
        }
    }
    /* 生产者全部结束后再排空一次：最后一条 push 可能晚于上一轮空读。 */
    if (drain_queue(q, &popped, &bad) != 0) return 5;

    accepted = 0;
    rejected = 0;
    errors = 0;
    for (i = 0; i < THREADS; i++) {
        accepted = accepted + prod[i].accepted;
        rejected = rejected + prod[i].rejected;
        errors = errors + prod[i].errors;
    }

    if (errors != 0) {
        fprintf(stderr, "queue_mpsc: %d 次 push 非容量错误\n", errors);
        return 6;
    }
    if (accepted + rejected != TOTAL) {
        fprintf(stderr, "queue_mpsc: accepted(%d) + rejected(%d) != %d\n",
                accepted, rejected, TOTAL);
        return 6;
    }
    if (popped != accepted) {
        fprintf(stderr, "queue_mpsc: 出队 %d 条，已接受 %d 条\n", popped, accepted);
        return 7;
    }
    for (t = 0; t < THREADS; t++) {
        for (i = 0; i < PER_THREAD; i++) {
            if (accepted_flag[t][i] != 0 && seen[t][i] != 1) {
                fprintf(stderr, "queue_mpsc: 已接受项丢失 t=%d i=%d seen=%d\n",
                        t, i, (int)seen[t][i]);
                return 8;
            }
            if (accepted_flag[t][i] == 0 && seen[t][i] != 0) {
                fprintf(stderr, "queue_mpsc: 未接受项却出队 t=%d i=%d\n", t, i);
                return 9;
            }
        }
    }
    if (bad != 0) {
        fprintf(stderr, "queue_mpsc: %d 条负载/重复错误\n", bad);
        return 10;
    }

    if (queue_close(q) != 0) {
        fprintf(stderr, "queue_mpsc: queue_close 失败\n");
        return 11;
    }

    printf("QUEUE MPSC OK accepted=%d rejected=%d popped=%d\n",
           accepted, rejected, popped);
    return 0;
}
