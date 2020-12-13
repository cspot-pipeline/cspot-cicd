#include "czmq.h"
#include "monitor.h"
#include "raft.h"
#include "raft_utils.h"
#include "woofc.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct invoke_thread_arg {
    RAFT_CLIENT_PUT_REQUEST request;
    RAFT_CLIENT_PUT_RESULT result;
} INVOKE_THREAD_ARG;

void* invoke_thread(void* arg) {
    INVOKE_THREAD_ARG* invoke_thread_arg = (INVOKE_THREAD_ARG*)arg;
    unsigned long result_seq;
    if (invoke_thread_arg->request.callback_handler[0] != 0) {
        result_seq = WooFPut(invoke_thread_arg->request.callback_woof,
                             invoke_thread_arg->request.callback_handler,
                             &(invoke_thread_arg->result));
    } else {
        result_seq = WooFPut(invoke_thread_arg->request.callback_woof, NULL, &(invoke_thread_arg->result));
    }
    if (WooFInvalid(result_seq)) {
        log_error("failed to put client_put_result to %s", invoke_thread_arg->request.callback_woof);
        exit(1);
    }
    log_error("client_put took %lu ms to be processed and %lu ms to invoke (%lu)",
              invoke_thread_arg->result.picked_ts - invoke_thread_arg->request.created_ts,
              get_milliseconds() - invoke_thread_arg->result.picked_ts,
              invoke_thread_arg->result.redirected_time);
}

int invoke_client_put_handler(RAFT_SERVER_STATE* server_state, RAFT_INVOKE_COMMITTED_ARG* arg) {
    unsigned long latest_result_seqno = WooFGetLatestSeqno(RAFT_CLIENT_PUT_RESULT_WOOF);
    if (WooFInvalid(latest_result_seqno)) {
        log_error("failed to get the latest seqno of %s", RAFT_CLIENT_PUT_RESULT_WOOF);
        return -1;
    }
    uint64_t begin = get_milliseconds();
    int pending = latest_result_seqno - arg->last_checked_client_put_result_seqno;
    INVOKE_THREAD_ARG* invoke_thread_arg = malloc(sizeof(INVOKE_THREAD_ARG) * pending);
    pthread_t* invoke_thread_id = malloc(sizeof(pthread_t) * pending);
    int count = 0;
    unsigned int i;
    for (i = arg->last_checked_client_put_result_seqno + 1; i <= latest_result_seqno; ++i) {
        RAFT_CLIENT_PUT_REQUEST* request = &(invoke_thread_arg[count].request);
        if (WooFGet(RAFT_CLIENT_PUT_REQUEST_WOOF, request, i) < 0) {
            log_error("failed to get the client_put request at %lu", i);
            threads_join(count, invoke_thread_id);
            free(invoke_thread_arg);
            free(invoke_thread_id);
            return -1;
        }
        // log_debug("checking result %lu", i);
        RAFT_CLIENT_PUT_RESULT* result = &(invoke_thread_arg[count].result);
        if (WooFGet(RAFT_CLIENT_PUT_RESULT_WOOF, result, i) < 0) {
            log_error("failed to get the client_put result at %lu", i);
            threads_join(count, invoke_thread_id);
            free(invoke_thread_arg);
            free(invoke_thread_id);
            return -1;
        }
        if (result->index == 0 || request->callback_woof[0] == 0) {
            arg->last_checked_client_put_result_seqno = i;
            continue;
        } else if (result->index <= server_state->commit_index) {
            // log entry has been committed, invoke the handler
            if (pthread_create(&invoke_thread_id[count], NULL, invoke_thread, (void*)&invoke_thread_arg[count]) < 0) {
                log_error("failed to create thread to invoke callback of client_put request");
                threads_join(count, invoke_thread_id);
                free(invoke_thread_arg);
                free(invoke_thread_id);
                return -1;
            }
            ++count;
            arg->last_checked_client_put_result_seqno = i;
        } else {
            break;
        }
    }
    if (count != 0) {
        log_error("waiting for %d client_put callback to be sent", count);
    }
    threads_join(count, invoke_thread_id);
    free(invoke_thread_arg);
    free(invoke_thread_id);
    if (count != 0) {
        log_debug("sent %d client_put: %lu ms", count, get_milliseconds() - begin);
    }
    return 0;
}

int h_invoke_committed(WOOF* wf, unsigned long seq_no, void* ptr) {
    log_set_tag("h_invoke_committed");
    log_set_level(RAFT_LOG_INFO);
    log_set_level(RAFT_LOG_DEBUG);
    log_set_output(stdout);

    RAFT_INVOKE_COMMITTED_ARG arg = {0};
    if (monitor_cast(ptr, &arg, sizeof(RAFT_CHECK_APPEND_RESULT_ARG)) < 0) {
        log_error("failed to monitor_cast");
        exit(1);
    }
    seq_no = monitor_seqno(ptr);

    // zsys_init() is called automatically when a socket is created
    // not thread safe and can only be called in main thread
    // call it here to avoid being called concurrently in the threads
    zsys_init();

    // get the server's current term and cluster members
    RAFT_SERVER_STATE server_state = {0};
    if (get_server_state(&server_state) < 0) {
        log_error("failed to get the server state");
        exit(1);
    }

    if (server_state.current_term != arg.term || server_state.role != RAFT_LEADER) {
        log_debug(
            "not a leader at term %" PRIu64 " anymore, current term: %" PRIu64 "", arg.term, server_state.current_term);
        monitor_join();
        return 1;
    }

    // check previous append_entries_result
    int err = invoke_client_put_handler(&server_state, &arg);
    if (err == 1) { // not leader anymore
        monitor_exit(ptr);
        monitor_join();
        return 1;
    } else if (err < 0) {
        exit(1);
    }

    unsigned long seq = WooFPut(RAFT_SERVER_STATE_WOOF, NULL, &server_state);
    if (WooFInvalid(seq)) {
        log_error("failed to update server state");
        exit(1);
    }

    seq = monitor_put(RAFT_MONITOR_NAME, RAFT_INVOKE_COMMITTED_WOOF, "h_invoke_committed", &arg, 1);
    if (WooFInvalid(seq)) {
        log_error("failed to queue the next h_invoke_committed handler");
        exit(1);
    }

    monitor_exit(ptr);
    monitor_join();
    return 1;
}