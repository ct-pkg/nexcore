/*
 * tests/drain_block.c — nexcore_drain 的阻塞、唤醒与单 drain 准入（PH15-T2）。
 *
 * 判据（docs/phases/PH-15/card.md §4/§5 E3、E4）：
 *   1. 空队列 drain(-1) 在 200 ms 时仍阻塞；
 *   2. 另一线程 send 后 ≤100 ms 返回 NX_OK 且 *out_count == 1；
 *   3. shutdown 后 ≤100 ms 返回 NX_E_SHUTDOWN 且 *out_count == 0；
 *   4. 双线程并发 drain ×100 次：恰好一个收到（阻塞后被 send 唤醒），
 *      另一个立即拿到 NX_E_THREAD。
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "NexCore.h"

#define ROUNDS 100
#define WAKE_BUDGET_MS 100
#define BLOCK_OBSERVE_MS 200
#define THREAD_TIMEOUT_MS 3000

struct sink_state {
    int count;
    size_t last_len;
};

static void on_message(void *user, const char *json, size_t len)
{
    struct sink_state *st = user;

    (void)json;
    st->count = st->count + 1;
    st->last_len = len;
}

static int64_t now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

static int make_handle(NexCoreHandle *out, struct sink_state *sink)
{
    struct NexCoreConfig cfg;
    int32_t rc;

    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = (uint32_t)sizeof(cfg);
    cfg.abi_version = NEXCORE_ABI_VERSION;
    cfg.protocol_version = NEXCORE_PROTOCOL_VERSION;
    cfg.default_timeout_ms = NEXCORE_DEFAULT_TIMEOUT_MS;
    cfg.max_message_bytes = NEXCORE_DEFAULT_MAX_MESSAGE_BYTES;
    cfg.max_pending = NEXCORE_DEFAULT_MAX_PENDING;
    cfg.bootstrap_js = "";
    cfg.bootstrap_len = 0;
    rc = nexcore_create(&cfg, out);
    if (rc != NX_OK) return rc;
    return nexcore_set_sink(*out, sink, on_message);
}

struct drain_call {
    NexCoreHandle h;
    int32_t wait_ms;
    int32_t rc;
    int32_t count;
    int64_t start_ms;
    int64_t end_ms;
    int started;
    int done;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

static void call_init(struct drain_call *c)
{
    memset(c, 0, sizeof(*c));
    c->rc = -1;
    c->count = -1;
    pthread_mutex_init(&c->lock, 0);
    pthread_cond_init(&c->cond, 0);
}

static void call_destroy(struct drain_call *c)
{
    pthread_mutex_destroy(&c->lock);
    pthread_cond_destroy(&c->cond);
}

static void *drain_thread(void *arg)
{
    struct drain_call *c = arg;

    pthread_mutex_lock(&c->lock);
    c->started = 1;
    pthread_cond_broadcast(&c->cond);
    pthread_mutex_unlock(&c->lock);

    c->start_ms = now_ms();
    c->rc = nexcore_drain(c->h, c->wait_ms, &c->count);
    c->end_ms = now_ms();

    pthread_mutex_lock(&c->lock);
    c->done = 1;
    pthread_cond_broadcast(&c->cond);
    pthread_mutex_unlock(&c->lock);
    return 0;
}

static int wait_started(struct drain_call *c)
{
    struct timespec deadline;
    int ok = 0;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec = deadline.tv_sec + 3;
    pthread_mutex_lock(&c->lock);
    while (c->started == 0 && ok == 0) {
        if (pthread_cond_timedwait(&c->cond, &c->lock, &deadline) != 0) ok = -1;
    }
    pthread_mutex_unlock(&c->lock);
    return ok;
}

static int wait_done(struct drain_call *c, int timeout_ms)
{
    struct timespec deadline;
    int ok = 0;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec = deadline.tv_sec + timeout_ms / 1000;
    deadline.tv_nsec = deadline.tv_nsec + (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_nsec = deadline.tv_nsec - 1000000000L;
        deadline.tv_sec = deadline.tv_sec + 1;
    }
    pthread_mutex_lock(&c->lock);
    while (c->done == 0 && ok == 0) {
        if (pthread_cond_timedwait(&c->cond, &c->lock, &deadline) != 0) ok = -1;
    }
    ok = c->done == 1 ? 0 : -1;
    pthread_mutex_unlock(&c->lock);
    return ok;
}

static int is_done(struct drain_call *c)
{
    int done;

    pthread_mutex_lock(&c->lock);
    done = c->done;
    pthread_mutex_unlock(&c->lock);
    return done;
}

static int start_call(struct drain_call *c, NexCoreHandle h, int32_t wait_ms,
                      pthread_t *tid)
{
    call_init(c);
    c->h = h;
    c->wait_ms = wait_ms;
    if (pthread_create(tid, 0, drain_thread, c) != 0) return -1;
    return wait_started(c);
}

/* 1 + 2：空队列 drain(-1) 真阻塞；send 后在预算内返回且投递 1 条。 */
static int scenario_block_and_send(void)
{
    struct sink_state sink;
    struct drain_call c;
    NexCoreHandle h = 0;
    pthread_t tid;
    const char *msg = "{\"id\":1,\"method\":\"a\"}";
    int64_t sent_ms;
    int64_t wake_ms;
    int32_t rc;

    memset(&sink, 0, sizeof(sink));
    rc = make_handle(&h, &sink);
    if (rc != NX_OK) {
        fprintf(stderr, "drain_block: create rc=%d\n", (int)rc);
        return 1;
    }
    if (start_call(&c, h, -1, &tid) != 0) return 1;

    usleep(BLOCK_OBSERVE_MS * 1000);
    if (is_done(&c)) {
        fprintf(stderr, "drain_block: drain(-1) 在 %d ms 时就已返回（rc=%d count=%d），未阻塞\n",
                BLOCK_OBSERVE_MS, (int)c.rc, (int)c.count);
        return 3;
    }

    sent_ms = now_ms();
    if (nexcore_send(h, msg, strlen(msg)) != NX_OK) return 4;
    if (wait_done(&c, THREAD_TIMEOUT_MS) != 0) {
        fprintf(stderr, "drain_block: send 后 drain 未在 %d ms 内返回\n", THREAD_TIMEOUT_MS);
        return 5;
    }
    wake_ms = now_ms() - sent_ms;
    if (pthread_join(tid, 0) != 0) return 6;

    printf("drain_block: block+send rc=%d count=%d wake=%lldms sink=%d\n",
           (int)c.rc, (int)c.count, (long long)wake_ms, sink.count);
    if (c.rc != NX_OK || c.count != 1 || sink.count != 1) return 7;
    if (wake_ms > WAKE_BUDGET_MS) {
        fprintf(stderr, "drain_block: send 唤醒耗时 %lld ms > %d ms\n",
                (long long)wake_ms, WAKE_BUDGET_MS);
        return 8;
    }

    call_destroy(&c);
    if (nexcore_shutdown(h) != NX_OK) return 9;
    if (nexcore_free(h) != NX_OK) return 10;
    return 0;
}

/* 3：阻塞中的 drain 被 shutdown 唤醒，预算内返回 NX_E_SHUTDOWN。 */
static int scenario_shutdown_wake(void)
{
    struct sink_state sink;
    struct drain_call c;
    NexCoreHandle h = 0;
    pthread_t tid;
    int64_t shut_ms;
    int64_t wake_ms;
    int32_t rc;

    memset(&sink, 0, sizeof(sink));
    rc = make_handle(&h, &sink);
    if (rc != NX_OK) return 20;
    if (start_call(&c, h, -1, &tid) != 0) return 20;

    usleep(50000);
    if (is_done(&c)) {
        fprintf(stderr, "drain_block: shutdown 前 drain(-1) 已返回（rc=%d）\n", (int)c.rc);
        return 21;
    }

    shut_ms = now_ms();
    if (nexcore_shutdown(h) != NX_OK) return 22;
    if (wait_done(&c, THREAD_TIMEOUT_MS) != 0) {
        fprintf(stderr, "drain_block: shutdown 后 drain 未在 %d ms 内返回\n", THREAD_TIMEOUT_MS);
        return 23;
    }
    wake_ms = now_ms() - shut_ms;
    if (pthread_join(tid, 0) != 0) return 24;

    printf("drain_block: shutdown rc=%d count=%d wake=%lldms\n",
           (int)c.rc, (int)c.count, (long long)wake_ms);
    if (c.rc != NX_E_SHUTDOWN || c.count != 0) return 25;
    if (wake_ms > WAKE_BUDGET_MS) {
        fprintf(stderr, "drain_block: shutdown 唤醒耗时 %lld ms > %d ms\n",
                (long long)wake_ms, WAKE_BUDGET_MS);
        return 26;
    }

    call_destroy(&c);
    if (nexcore_free(h) != NX_OK) return 27;
    return 0;
}

/*
 * 4：每轮一个线程 drain(-1) 阻塞，主线程反复 drain(0) 直到拿到 NX_E_THREAD；
 * 随后 send 唤醒阻塞者，确认恰好一个收到、另一个被拒绝。
 */
static int scenario_single_admission(void)
{
    struct sink_state sink;
    NexCoreHandle h = 0;
    const char *msg = "{\"id\":2,\"method\":\"b\"}";
    int32_t rc;
    int round;

    memset(&sink, 0, sizeof(sink));
    rc = make_handle(&h, &sink);
    if (rc != NX_OK) return 40;

    for (round = 0; round < ROUNDS; round++) {
        struct drain_call c;
        pthread_t tid;
        int32_t probe = NX_OK;
        int32_t probe_count = -1;
        int tries;
        int64_t sent_ms;
        int64_t wake_ms;

        if (start_call(&c, h, -1, &tid) != 0) return 41;

        for (tries = 0; tries < 2000; tries++) {
            probe_count = -1;
            probe = nexcore_drain(h, 0, &probe_count);
            if (probe == NX_E_THREAD) {
                if (probe_count != -1 && probe_count != 0) {
                    fprintf(stderr, "drain_block: 被拒绝的 drain 仍写了 out_count=%d\n",
                            (int)probe_count);
                    return 42;
                }
                break;
            }
            if (probe != NX_OK) {
                fprintf(stderr, "drain_block: 并发 drain(0) 返回 %d（期望 NX_OK 或 NX_E_THREAD）\n",
                        (int)probe);
                return 43;
            }
            usleep(1000);
        }
        if (probe != NX_E_THREAD) {
            fprintf(stderr, "drain_block: 第 %d 轮未观察到单 drain 准入（%d 次探测）\n",
                    round, tries);
            return 44;
        }
        if (is_done(&c)) return 45;

        sent_ms = now_ms();
        if (nexcore_send(h, msg, strlen(msg)) != NX_OK) return 46;
        if (wait_done(&c, THREAD_TIMEOUT_MS) != 0) {
            fprintf(stderr, "drain_block: 第 %d 轮阻塞 drain 未被 send 唤醒\n", round);
            return 47;
        }
        wake_ms = now_ms() - sent_ms;
        if (pthread_join(tid, 0) != 0) return 48;
        if (c.rc != NX_OK || c.count != 1) {
            fprintf(stderr, "drain_block: 第 %d 轮 rc=%d count=%d\n",
                    round, (int)c.rc, (int)c.count);
            return 49;
        }
        if (wake_ms > WAKE_BUDGET_MS) {
            fprintf(stderr, "drain_block: 第 %d 轮唤醒耗时 %lld ms\n", round,
                    (long long)wake_ms);
            return 50;
        }
        if (sink.count != round + 1) {
            fprintf(stderr, "drain_block: 第 %d 轮 sink.count=%d\n", round, sink.count);
            return 51;
        }
        call_destroy(&c);
    }

    printf("drain_block: admission rounds=%d sink=%d\n", ROUNDS, sink.count);
    if (nexcore_shutdown(h) != NX_OK) return 52;
    if (nexcore_free(h) != NX_OK) return 53;
    return 0;
}

int main(void)
{
    int rc;

    rc = scenario_block_and_send();
    if (rc != 0) return rc;
    rc = scenario_shutdown_wake();
    if (rc != 0) return rc;
    rc = scenario_single_admission();
    if (rc != 0) return rc;

    printf("DRAIN BLOCK OK\n");
    return 0;
}
