#include "dht.h"
#include "dht_utils.h"
#include "monitor.h"
#include "woofc.h"

#include <stdlib.h>
#include <string.h>

int h_fix_finger_callback(WOOF* wf, unsigned long seq_no, void* ptr) {
    log_set_tag("fix_finger_callback");
    log_set_level(DHT_LOG_INFO);
    // log_set_level(DHT_LOG_DEBUG);
    log_set_output(stdout);

    DHT_FIX_FINGER_CALLBACK_ARG arg = {0};
	if (monitor_cast(ptr, &arg) < 0) {
        log_error("failed to call monitor_cast");
        exit(1);
    }

    char woof_name[DHT_NAME_LENGTH];
    if (node_woof_name(woof_name) < 0) {
        log_error("failed to get local node's woof name");
		monitor_exit(ptr);
        exit(1);
    }

    log_debug("find_successor leader: %s(%d)", arg.node_replicas[arg.node_leader], arg.node_leader);

    // finger[i] = find_successor(x);
    int i = arg.finger_index;
    DHT_FINGER_INFO finger = {0};
    if (get_latest_finger_info(i, &finger) < 0) {
        log_error("failed to get finger[%d]'s info: %s", i, dht_error_msg);
		monitor_exit(ptr);
        exit(1);
    }
    // sprintf(msg, "current finger_addr[%d] = %s, %s", i, dht_table.finger_addr[i], result->node_addr);
    // log_info("fix_fingers_callback", msg);
    if (memcmp(finger.hash, arg.node_hash, SHA_DIGEST_LENGTH) == 0) {
        log_debug("no need to update finger_addr[%d]", i);
		monitor_exit(ptr);
        return 1;
    }

    memcpy(finger.hash, arg.node_hash, sizeof(finger.hash));
    memcpy(finger.replicas, arg.node_replicas, sizeof(finger.replicas));
    finger.leader = arg.node_leader;
    unsigned long seq = set_finger_info(i, &finger);
    if (WooFInvalid(seq)) {
        log_error("failed to update finger[%d]", i);
		monitor_exit(ptr);
        exit(1);
    }

    char hash_str[2 * SHA_DIGEST_LENGTH + 1];
    print_node_hash(hash_str, finger.hash);
    log_debug("updated finger_addr[%d] = %s(%s)", i, arg.node_replicas[arg.node_leader], hash_str);

	monitor_exit(ptr);
    return 1;
}
