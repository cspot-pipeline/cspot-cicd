#include "dht.h"
#include "dht_utils.h"
#include "raft_client.h"
#include "string.h"
#include "woofc.h"

typedef struct resolve_thread_arg {
    char node_addr[DHT_NAME_LENGTH];
    unsigned long seq_no;
} RESOLVE_THREAD_ARG;

void* resolve_thread(void* arg) {
    RESOLVE_THREAD_ARG* thread_arg = (RESOLVE_THREAD_ARG*)arg;
    DHT_SERVER_PUBLISH_DATA_ARG data_arg = {0};
    if (WooFGet(DHT_SERVER_PUBLISH_DATA_WOOF, &data_arg, thread_arg->seq_no) < 0) {
        log_error("failed to get server_data_find_arg at %lu", thread_arg->seq_no);
        return;
    }
    // log_error("data_arg.created_ts: %lu", data_arg.created_ts);
    // log_error("data_arg.find_ts: %lu (%lu)", data_arg.find_ts, data_arg.find_ts - data_arg.created_ts);
    // log_error("data_arg.processed: %lu (%lu)", get_milliseconds(), get_milliseconds() - data_arg.find_ts);

    DHT_SERVER_PUBLISH_FIND_ARG find_arg = {0};
    if (WooFGet(DHT_SERVER_PUBLISH_FIND_WOOF, &find_arg, data_arg.find_arg_seqno) < 0) {
        log_error("failed to get server_publish_find_arg at %lu", data_arg.find_arg_seqno);
        return;
    }

    char registration_woof[DHT_NAME_LENGTH] = {0};
    char registration_monitor[DHT_NAME_LENGTH] = {0};
    DHT_TOPIC_REGISTRY topic_entry = {0};
    char* node_addr;
    int i, j;
    for (i = 0; i < DHT_REPLICA_NUMBER; ++i) {
        node_addr = data_arg.node_replicas[(data_arg.node_leader + i) % DHT_REPLICA_NUMBER];
        // get the topic namespace
        sprintf(registration_woof, "%s/%s_%s", node_addr, find_arg.topic_name, DHT_TOPIC_REGISTRATION_WOOF);
        sprintf(registration_monitor, "%s/%s", node_addr, DHT_MONITOR_NAME);
        if (get_latest_element(registration_woof, &topic_entry) < 0) {
            log_warn("%s not working: %s", registration_woof, dht_error_msg);
        } else {
            // found a working replica
            // log_debug("%s works", registration_woof);
            break;
        }
    }
    if (i == DHT_REPLICA_NUMBER) {
        char err_msg[DHT_NAME_LENGTH] = {0};
        strcpy(err_msg, dht_error_msg);
        log_error("failed to get topic registration info from %s: %s", node_addr, err_msg);
        return;
    }
    DHT_TRIGGER_ARG partial_trigger_arg = {0};
    partial_trigger_arg.requested = find_arg.requested_ts;
    partial_trigger_arg.found = get_milliseconds();
    strcpy(partial_trigger_arg.topic_name, data_arg.topic_name);
    sprintf(
        partial_trigger_arg.subscription_woof, "%s/%s_%s", node_addr, data_arg.topic_name, DHT_SUBSCRIPTION_LIST_WOOF);

    char trigger_woof[DHT_NAME_LENGTH] = {0};
    // put data to raft
    int working_replica_id = -1;
    char* topic_replica;
    for (i = 0; i < DHT_REPLICA_NUMBER; ++i) {
        topic_replica = topic_entry.topic_replicas[(topic_entry.last_leader + i) % DHT_REPLICA_NUMBER];
        sprintf(partial_trigger_arg.element_woof, "%s", topic_replica);
        RAFT_DATA_TYPE raft_data = {0};
        memcpy(raft_data.val, find_arg.element, find_arg.element_size);

        unsigned long trigger_seq = WooFPut(DHT_PARTIAL_TRIGGER_WOOF, NULL, &partial_trigger_arg);
        if (WooFInvalid(trigger_seq)) {
            log_error("failed to store partial trigger to %s", DHT_PARTIAL_TRIGGER_WOOF);
            return;
        }

        RAFT_CLIENT_PUT_OPTION opt = {0};
        sprintf(opt.callback_woof, "%s/%s", thread_arg->node_addr, DHT_SERVER_PUBLISH_MAP_WOOF);
        sprintf(opt.extra_woof, "%s", DHT_PARTIAL_TRIGGER_WOOF);
        opt.extra_seqno = trigger_seq;
        unsigned long seq = raft_put(topic_replica, &raft_data, &opt);
        if (raft_is_error(seq)) {
            log_error("failed to put data to raft: %s", raft_error_msg);
            return;
        }
        log_debug("requested to put data to raft, client_put seqno: %lu", seq);
        break;
    }
    if (i == DHT_REPLICA_NUMBER) {
        log_error("failed to put data to raft: %s", "none of replicas is working leader");
        return;
    }
}

int server_publish_data(WOOF* wf, unsigned long seq_no, void* ptr) {
    DHT_LOOP_ROUTINE_ARG* routine_arg = (DHT_LOOP_ROUTINE_ARG*)ptr;

    log_set_tag("server_publish_data");
    log_set_level(DHT_LOG_INFO);
    log_set_level(DHT_LOG_DEBUG);
    log_set_output(stdout);

    uint64_t begin = get_milliseconds();

    DHT_NODE_INFO node = {0};
    if (get_latest_node_info(&node) < 0) {
        log_error("couldn't get latest node info: %s", dht_error_msg);
        exit(1);
    }

    unsigned long latest_seq = WooFGetLatestSeqno(DHT_SERVER_PUBLISH_DATA_WOOF);
    if (WooFInvalid(latest_seq)) {
        log_error("failed to get the latest seqno from %s", DHT_SERVER_PUBLISH_DATA_WOOF);
        exit(1);
    }

    int count = latest_seq - routine_arg->last_seqno;
    if (count != 0) {
        log_debug("processing %d publish_data", count);
    }

    zsys_init();
    RESOLVE_THREAD_ARG thread_arg[count];
    pthread_t thread_id[count];

    int i;
    for (i = 0; i < count; ++i) {
        memcpy(thread_arg[i].node_addr, node.addr, sizeof(thread_arg[i].node_addr));
        thread_arg[i].seq_no = routine_arg->last_seqno + 1 + i;
        if (pthread_create(&thread_id[i], NULL, resolve_thread, (void*)&thread_arg[i]) < 0) {
            log_error("failed to create resolve_thread to process publish_data");
            exit(1);
        }
    }
    threads_join(count, thread_id);

    if (count != 0) {
        if (get_milliseconds() - begin > 200)
        log_debug("took %lu ms to process %lu publish_data", get_milliseconds() - begin, count);
    }
    routine_arg->last_seqno = latest_seq;
    unsigned long seq = WooFPut(DHT_SERVER_LOOP_ROUTINE_WOOF, "server_publish_data", routine_arg);
    if (WooFInvalid(seq)) {
        log_error("failed to queue the next server_publish_data");
        exit(1);
    }

    return 1;
}