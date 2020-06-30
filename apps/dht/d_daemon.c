#include "dht.h"
#include "dht_utils.h"
#include "monitor.h"
#include "woofc.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef USE_RAFT
#include "raft.h"
#endif

int d_daemon(WOOF* wf, unsigned long seq_no, void* ptr) {
    DHT_DAEMON_ARG* arg = (DHT_DAEMON_ARG*)ptr;

    log_set_tag("daemon");
    log_set_level(DHT_LOG_DEBUG);
    log_set_level(DHT_LOG_INFO);
    log_set_output(stdout);

    unsigned long now = get_milliseconds();

#ifdef USE_RAFT
    if (now - arg->last_update_leader_id > DHT_UPDATE_LEADER_ID_FREQUENCY) {
        int leader_id = raft_leader_id();
        if (leader_id < 0) {
            log_error("failed to get replica's current leader_id: %s", dht_error_msg);
        } else {
            DHT_NODE_INFO node = {0};
            if (get_latest_node_info(&node) < 0) {
                log_error("couldn't get latest node info: %s", dht_error_msg);
                exit(1);
            }
            node.leader_id = leader_id;
            unsigned long seq = WooFPut(DHT_NODE_INFO_WOOF, NULL, &node);
            if (WooFInvalid(seq)) {
                log_error("failed to update replica's leader_id to %d", node.leader_id);
                exit(1);
            }
            log_debug("updated replica's leader_id to %d", node.leader_id);
        }
        arg->last_update_leader_id = now;
    }
    if (!raft_is_leader()) {
        log_debug("not a raft leader, went to sleep");
        unsigned long seq = WooFPut(DHT_DAEMON_WOOF, "d_daemon", arg);
        if (WooFInvalid(seq)) {
            log_error("failed to invoke next d_daemon");
            exit(1);
        }
        return 1;
    }
#endif

    // log_debug("since last stabilize: %lums", now - arg->last_stabilize);
    // log_debug("since last check_predecessor: %lums", now - arg->last_check_predecessor);
    // log_debug("since last fix_finger: %lums", now - arg->last_fix_finger);

    if (now - arg->last_stabilize > DHT_STABILIZE_FREQUENCY) {
        DHT_STABILIZE_ARG stabilize_arg;
        unsigned long seq = monitor_put(DHT_MONITOR_NAME, DHT_STABILIZE_WOOF, "d_stabilize", &stabilize_arg, 1);
        if (WooFInvalid(seq)) {
            log_error("failed to invoke d_stabilize");
        }
        arg->last_stabilize = now;
    }

    if (now - arg->last_check_predecessor > DHT_CHECK_PREDECESSOR_FREQUENCY) {
        DHT_CHECK_PREDECESSOR_ARG check_predecessor_arg;
        unsigned long seq = WooFPut(DHT_CHECK_PREDECESSOR_WOOF, "d_check_predecessor", &check_predecessor_arg);
        if (WooFInvalid(seq)) {
            log_error("failed to invoke d_check_predecessor");
        }
        arg->last_check_predecessor = now;
    }

    if (now - arg->last_fix_finger > DHT_FIX_FINGER_FREQUENCY) {
        DHT_FIX_FINGER_ARG fix_finger_arg = {0};
        fix_finger_arg.finger_index = arg->last_fixed_finger_index;
        unsigned long seq = WooFPut(DHT_FIX_FINGER_WOOF, "d_fix_finger", &fix_finger_arg);
        if (WooFInvalid(seq)) {
            log_error("failed to invoke d_fix_finger");
        }
        arg->last_fix_finger = now;
        ++arg->last_fixed_finger_index;
        if (arg->last_fixed_finger_index > SHA_DIGEST_LENGTH * 8) {
            arg->last_fixed_finger_index = 1;
        }
    }

    // #ifdef USE_RAFT
    //     if (now - arg->last_replicate_state > DHT_REPLICATE_STATE_FREQUENCY) {
    //         DHT_REPLICATE_STATE_ARG replicate_state_arg;
    //         unsigned long seq = WooFPut(DHT_REPLICATE_STATE_WOOF, "d_replicate_state", &replicate_state_arg);
    //         if (WooFInvalid(seq)) {
    //             log_error("failed to invoke d_replicate_state");
    //         }
    //         arg->last_replicate_state = now;
    //     }
    // #endif

	usleep(DHT_DAEMON_WAKEUP_FREQUENCY * 1000);
    unsigned long seq = WooFPut(DHT_DAEMON_WOOF, "d_daemon", arg);
    if (WooFInvalid(seq)) {
        log_error("failed to invoke next d_daemon");
        exit(1);
    }
    return 1;
}
