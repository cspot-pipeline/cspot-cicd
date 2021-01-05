#include "dht.h"
#include "dht_utils.h"
#include "raft_client.h"
#include "string.h"
#include "woofc.h"

#define MAX_PUBLISH_SIZE 64
#define PROFILING

#define SUBSCRIPTION_CACHE_SIZE 8

typedef struct subscription_cache {
    char woof_name[DHT_NAME_LENGTH];
    DHT_SUBSCRIPTION_LIST subscription_list;
} SUBSCRIPTION_CACHE;

SUBSCRIPTION_CACHE subscription_cache[SUBSCRIPTION_CACHE_SIZE];
pthread_mutex_t cache_lock;

int get_subscription_list(char* woof_name) {
    int i;
    for (i = 0; i < SUBSCRIPTION_CACHE_SIZE && subscription_cache[i].woof_name[0] != 0; ++i) {
        if (strcmp(subscription_cache[i].woof_name, woof_name) == 0) {
            return i;
        }
    }
    return -1;
}

int get_free_cache() {
    int i;
    while (i < SUBSCRIPTION_CACHE_SIZE && subscription_cache[i].woof_name[0] != 0) {
        ++i;
    }
    if (i == SUBSCRIPTION_CACHE_SIZE) {
        return -1;
    }
    return i;
}

void put_subscription_list(int cache_id, char* woof_name, DHT_SUBSCRIPTION_LIST* list) {
    memcpy(&subscription_cache[cache_id].woof_name, woof_name, sizeof(subscription_cache[cache_id].woof_name));
    memcpy(&subscription_cache[cache_id].subscription_list, list, sizeof(DHT_SUBSCRIPTION_LIST));
}

int update_subscription_list(char* subscription_woof, DHT_SUBSCRIBE_ARG* subscribe_arg) {
    char sub_ipaddr[DHT_NAME_LENGTH] = {0};
    if (WooFIPAddrFromURI(subscription_woof, sub_ipaddr, DHT_NAME_LENGTH) < 0) {
        log_error("failed to extract woof ip address from %s", subscription_woof);
        return -1;
    }
    int sub_port = 0;
    WooFPortFromURI(subscription_woof, &sub_port);
    char sub_namespace[DHT_NAME_LENGTH] = {0};
    if (WooFNameSpaceFromURI(subscription_woof, sub_namespace, DHT_NAME_LENGTH) < 0) {
        log_error("failed to extract woof namespace from %s", subscription_woof);
        return -1;
    }
    char sub_monitor[DHT_NAME_LENGTH] = {0};
    char sub_woof[DHT_NAME_LENGTH] = {0};
    if (sub_port > 0) {
        sprintf(sub_monitor, "woof://%s:%d%s/%s", sub_ipaddr, sub_port, sub_namespace, DHT_MONITOR_NAME);
        sprintf(sub_woof, "woof://%s:%d%s/%s", sub_ipaddr, sub_port, sub_namespace, DHT_SUBSCRIBE_WOOF);
    } else {
        sprintf(sub_monitor, "woof://%s%s/%s", sub_ipaddr, sub_namespace, DHT_MONITOR_NAME);
        sprintf(sub_woof, "woof://%s%s/%s", sub_ipaddr, sub_namespace, DHT_SUBSCRIBE_WOOF);
    }
    unsigned long seq = monitor_remote_put(sub_monitor, sub_woof, "r_subscribe", subscribe_arg, 0);
    if (WooFInvalid(seq)) {
        return -1;
    }
    return 0;
}

typedef struct resolve_thread_arg {
    char woof_name[DHT_NAME_LENGTH];
    unsigned long trigger_seqno;
    unsigned long element_seqno;
} RESOLVE_THREAD_ARG;

void* resolve_thread(void* arg) {
    RESOLVE_THREAD_ARG* thread_arg = (RESOLVE_THREAD_ARG*)arg;
    DHT_TRIGGER_ARG trigger_arg = {0};
    if (WooFGet(thread_arg->woof_name, &trigger_arg, thread_arg->trigger_seqno) < 0) {
        log_error("failed to get trigger_arg from %s at %lu", thread_arg->woof_name, thread_arg->trigger_seqno);
        return;
    }
    trigger_arg.data = get_milliseconds();
    trigger_arg.element_seqno = thread_arg->element_seqno;

    DHT_SUBSCRIPTION_LIST list = {0};
    pthread_mutex_lock(&cache_lock);
    int cache_id = get_subscription_list(trigger_arg.subscription_woof);
    if (cache_id != -1) {
        log_debug("found subscription list in cache[%d] %s", cache_id, trigger_arg.subscription_woof);
        memcpy(&list, &subscription_cache[cache_id].subscription_list, sizeof(DHT_SUBSCRIPTION_LIST));
    } else {
        cache_id = get_free_cache();
        if (WooFGet(trigger_arg.subscription_woof, &list, 0) < 0) {
            log_error("failed to get the latest subscription list of %s: %s", trigger_arg.topic_name, dht_error_msg);
            pthread_mutex_unlock(&cache_lock);
            return;
        } else if (cache_id != -1) {
            put_subscription_list(cache_id, trigger_arg.subscription_woof, &list);
            log_debug("put subscription list into cache[%d] %s", cache_id, trigger_arg.subscription_woof);
        }
    }
    pthread_mutex_unlock(&cache_lock);
    uint64_t ts_subscription = get_milliseconds();

    int i, j, k;
    for (i = 0; i < list.size; ++i) {
        for (j = 0; j < DHT_REPLICA_NUMBER; ++j) {
            k = (list.last_successful_replica[i] + j) % DHT_REPLICA_NUMBER;
            if (list.replica_namespaces[i][k][0] == 0) {
                continue;
            }
            char invocation_woof[DHT_NAME_LENGTH];
            sprintf(invocation_woof, "%s/%s", list.replica_namespaces[i][k], DHT_INVOCATION_WOOF);
            unsigned long seq = WooFPut(invocation_woof, list.handlers[i], &trigger_arg);
            if (WooFInvalid(seq)) {
                log_error("failed to trigger handler %s in %s", list.handlers[i], list.replica_namespaces[i][k]);
            } else {
                log_debug("triggered handler %s/%s", list.replica_namespaces[i][k], list.handlers[i]);
                if (k != list.last_successful_replica[i]) {
                    DHT_SUBSCRIBE_ARG subscribe_arg = {0};
                    memcpy(subscribe_arg.handler, list.handlers[i], sizeof(subscribe_arg.handler));
                    subscribe_arg.replica_leader = k;
                    memcpy(subscribe_arg.replica_namespaces,
                           list.replica_namespaces[i],
                           sizeof(subscribe_arg.replica_namespaces));
                    memcpy(subscribe_arg.topic_name, trigger_arg.topic_name, sizeof(subscribe_arg.topic_name));
                    if (update_subscription_list(trigger_arg.subscription_woof, &subscribe_arg) < 0) {
                        log_error("failed to update last_successful_replica[%d] to %d", i, k);
                    } else {
                        log_debug("updated last_successful_replica[%d] to %d", i, k);
                    }
                }

                uint64_t found = trigger_arg.found - trigger_arg.requested;
                uint64_t data = trigger_arg.data - trigger_arg.found;
                uint64_t subscription = ts_subscription - trigger_arg.data;
                uint64_t triggered = get_milliseconds() - ts_subscription;
                uint64_t total = get_milliseconds() - trigger_arg.requested;
                printf("TRIGGERED: found: %lu data: %lu subscription: %lu triggered: %lu total: %lu\n",
                       found,
                       data,
                       subscription,
                       triggered,
                       total);
                break;
            }
        }
    }
    return;
}

int server_publish_trigger(WOOF* wf, unsigned long seq_no, void* ptr) {
    DHT_LOOP_ROUTINE_ARG* routine_arg = (DHT_LOOP_ROUTINE_ARG*)ptr;
    log_set_tag("server_publish_trigger");
    log_set_level(DHT_LOG_INFO);
    // log_set_level(DHT_LOG_DEBUG);
    log_set_output(stdout);
    WooFMsgCacheInit();
    zsys_init();

    uint64_t begin = get_milliseconds();

    unsigned long latest_seq = WooFGetLatestSeqno(DHT_SERVER_PUBLISH_TRIGGER_WOOF);
    if (WooFInvalid(latest_seq)) {
        log_error("failed to get the latest seqno from %s", DHT_SERVER_PUBLISH_TRIGGER_WOOF);
        WooFMsgCacheShutdown();
        exit(1);
    }
    int count = latest_seq - routine_arg->last_seqno;
    if (count > MAX_PUBLISH_SIZE) {
        log_warn("publish_trigger lag: %d", count);
        count = MAX_PUBLISH_SIZE;
    }
    if (count != 0) {
        log_debug("processing %d publish_trigger", count);
    }

    RESOLVE_THREAD_ARG thread_arg[count];
    pthread_t thread_id[count];

    int i;
    for (i = 0; i < count; ++i) {
        RAFT_CLIENT_PUT_RESULT put_result = {0};
        if (WooFGet(DHT_SERVER_PUBLISH_TRIGGER_WOOF, &put_result, routine_arg->last_seqno + 1 + i) < 0) {
            log_error("failed to get put_result at %lu", routine_arg->last_seqno + 1 + i);
            WooFMsgCacheShutdown();
            exit(1);
        }
        strcpy(thread_arg[i].woof_name, put_result.extra_woof);
        thread_arg[i].trigger_seqno = put_result.extra_seqno;
        thread_arg[i].element_seqno = put_result.index;
        if (pthread_create(&thread_id[i], NULL, resolve_thread, (void*)&thread_arg[i]) < 0) {
            log_error("failed to create resolve_thread to process publish_trigger");
            WooFMsgCacheShutdown();
            exit(1);
        }
    }
    routine_arg->last_seqno += count;

    unsigned long seq = WooFPut(DHT_SERVER_LOOP_ROUTINE_WOOF, "server_publish_trigger", routine_arg);
    if (WooFInvalid(seq)) {
        log_error("failed to queue the next server_publish_trigger");
        WooFMsgCacheShutdown();
        exit(1);
    }

    threads_join(count, thread_id);
    WooFMsgCacheShutdown();
    return 1;
}