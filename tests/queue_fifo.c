/* tests/queue_fifo.c — 队列 FIFO 契约：push 顺序 == pop 顺序，空队列返回 -4。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "queue.h"

#define Q_EMPTY (-4)

int main(void)
{
    void *q;
    char *data = 0;
    int32_t len = 0;
    int32_t rc;

    q = queue_open(4);
    if (q == 0) return 1;

    if (queue_push(q, "first", 5) != 0) return 2;
    if (queue_push(q, "second", 6) != 0) return 3;
    if (queue_push(q, "third", 5) != 0) return 4;
    if (queue_depth(q) != 3) return 5;

    rc = queue_pop(q, &data, &len);
    if (rc != 0 || len != 5 || strcmp(data, "first") != 0) return 6;
    free(data);

    rc = queue_pop(q, &data, &len);
    if (rc != 0 || len != 6 || strcmp(data, "second") != 0) return 7;
    free(data);

    if (queue_depth(q) != 1) return 8;

    rc = queue_pop(q, &data, &len);
    if (rc != 0 || len != 5 || strcmp(data, "third") != 0) return 9;
    free(data);

    if (queue_depth(q) != 0) return 10;

    data = 0;
    len = -1;
    rc = queue_pop(q, &data, &len);
    if (rc != Q_EMPTY || data != 0 || len != 0) return 11;

    if (queue_close(q) != 0) return 12;

    printf("QUEUE FIFO OK\n");
    return 0;
}
