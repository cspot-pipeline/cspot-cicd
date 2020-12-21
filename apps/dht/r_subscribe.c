#include "dht.h"
#include "dht_utils.h"
#include "monitor.h"
#include "woofc.h"

#include <stdlib.h>
#include <string.h>

int r_subscribe(WOOF* wf, unsigned long seq_no, void* ptr) {
    log_set_tag("r_subscribe");
    log_set_level(DHT_LOG_INFO);
    // log_set_level(DHT_LOG_DEBUG);
    log_set_output(stdout);
    WooFMsgCacheInit();

    DHT_SUBSCRIBE_ARG arg = {0};
    if (monitor_cast(ptr, &arg, sizeof(DHT_SUBSCRIBE_ARG)) < 0) {
        log_error("failed to call monitor_cast");
        monitor_exit(ptr);
        WooFMsgCacheShutdown();
        exit(1);
    }

    DHT_NODE_INFO node = {0};
    if (get_latest_node_info(&node) < 0) {
        log_error("couldn't get latest node info: %s", dht_error_msg);
        monitor_exit(ptr);
        WooFMsgCacheShutdown();
        exit(1);
    }

    DHT_SUCCESSOR_INFO successor = {0};
    if (get_latest_successor_info(&successor) < 0) {
        log_error("couldn't get latest successor info: %s", dht_error_msg);
        WooFMsgCacheShutdown();
        exit(1);
    }

    log_debug("calling h_subscribe for topic %s on self: %s", arg.topic_name, node.addr);
    unsigned long index = raft_put_handler(node.addr, "h_subscribe", &arg, sizeof(DHT_SUBSCRIBE_ARG), 1, NULL);
    if (raft_is_error(index)) {
        log_error("failed to invoke h_subscribe on %s: %s", node.addr, raft_error_msg);
        monitor_exit(ptr);
        WooFMsgCacheShutdown();
        exit(1);
    }

    int i;
    for (i = 0; i < DHT_REGISTER_TOPIC_REPLICA - 1; ++i) {
        char* successor_leader = successor_addr(&successor, i);
        if (successor_leader[0] == 0) {
            break;
        }
        log_debug("replicating h_subscribe for topic %s on successor[%d]: %s", arg.topic_name, i, successor_leader);
        unsigned long index =
            raft_put_handler(successor_leader, "h_subscribe", &arg, sizeof(DHT_SUBSCRIBE_ARG), 1, NULL);
        if (raft_is_error(index)) {
            log_error("failed to invoke h_subscribe on %s: %s", successor_leader, raft_error_msg);
            continue;
        }
    }

    monitor_exit(ptr);
    WooFMsgCacheShutdown();
    return 1;
}
