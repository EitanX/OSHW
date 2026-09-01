#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "rwlock.h"


// Opaque struct definition
struct Server_Log {
    // TODO: Implement internal log storage (e.g., dynamic buffer, linked list, etc.)
    char *buf;
    size_t len;
    size_t cap;
};

static void ensure_cap(struct Server_Log *L, size_t add) {
    if (L->len + add <= L->cap) return;

    size_t new_cap = L->cap;
    while (new_cap < L->len + add) new_cap = new_cap * 2;

    char *new_buf = malloc(new_cap);
    if (!new_buf) unix_error("malloc failed");
    memcpy(new_buf, L->buf, L->len);
    free(L->buf);
    L->buf = new_buf;
    L->cap = new_cap;

}

// Creates a new server log instance (stub)
server_log create_log() {
    // TODO: Allocate and initialize internal log structure
    static int once = 0;
    if (!once) {
        readers_writers_init();
        once = 1;
    }

    struct Server_Log *L = malloc(sizeof(*L));
    if (!L) unix_error("malloc(log) failed");

    L->buf = malloc(MAXBUF);
    if (!L->buf) unix_error("malloc(log buf) failed");

    L->len = 0;
    L->cap = MAXBUF;
    return L;
}


// Destroys and frees the log (stub)
void destroy_log(server_log log) {
    // TODO: Free all internal resources used by the log
    if (!log) return;
    free(log->buf);
    free(log);
}

// Returns dummy log content as string (stub)
int get_log(server_log log, char **dst) {
    // TODO: Return the full contents of the log as a dynamically allocated string
    if (!dst) return -1;

    reader_lock();

    *dst = malloc(log->len + 1);
    if (!*dst) unix_error("malloc(get_log) failed");

    memcpy(*dst, log->buf, log->len);
    (*dst)[log->len] = '\0';

    reader_unlock();
    return (int) log->len;
}


// Appends a new entry to the log (no-op stub)
void add_to_log(server_log log, const char *data, int data_len) {
    // TODO: Append the provided data to the log
    // This function should handle concurrent access
    if (!log || !data || data_len <= 0) return;

    writer_lock();

    ensure_cap(log, (size_t) data_len);
    memcpy(log->buf + log->len, data, (size_t) data_len);
    log->len += (size_t) data_len;

    writer_unlock();
}
