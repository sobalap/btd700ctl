#include <btd700/btd700_c.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;
static int g_saved_sink_id = -1;
static int g_sink_switched = 0;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* parse wpctl status output to find sink node IDs.
 * if out_btd700_id is non-NULL, stores the BTD700 sink node ID (or -1).
 * if out_default_id is non-NULL, stores the current default sink node ID (or -1). */
static void find_sinks(int* out_btd700_id, int* out_default_id) {
    if (out_btd700_id) *out_btd700_id = -1;
    if (out_default_id) *out_default_id = -1;

    FILE* fp = popen("wpctl status", "r");
    if (!fp) return;

    char line[512];
    int in_sinks = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "Sinks:")) {
            in_sinks = 1;
            continue;
        }

        if (in_sinks) {
            // end of sinks section: new section header or blank-ish line
            if (strstr(line, "Sources:") || strstr(line, "Devices:") ||
                strstr(line, "Streams:") || strstr(line, "Filters:")) {
                break;
            }

            // skip decorations
            const char* p = line;
            while (*p && (*p < '0' || *p > '9')) p++;

            int node_id = 0;
            if (sscanf(p, "%d.", &node_id) != 1)
                continue;

            int is_default = (strchr(line, '*') != NULL);

            if (out_default_id && is_default)
                *out_default_id = node_id;

            if (out_btd700_id &&
                (strcasestr(line, "sennheiser") || strcasestr(line, "btd")))
                *out_btd700_id = node_id;
        }
    }

    pclose(fp);
}

static int set_default_sink(int node_id) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "wpctl set-default %d", node_id);
    return system(cmd);
}

static void switch_to_btd700(void) {
    if (g_sink_switched) return;

    int btd_id = -1, default_id = -1;
    find_sinks(&btd_id, &default_id);

    if (btd_id < 0) {
        fprintf(stderr, "btd700d: BTD700 sink not found in PipeWire\n");
        return;
    }

    if (default_id == btd_id) {
        g_sink_switched = 1;
        return;
    }

    g_saved_sink_id = default_id;
    set_default_sink(btd_id);
    g_sink_switched = 1;
    fprintf(stderr, "btd700d: switched default sink to BTD700 (node %d), saved previous (node %d)\n",
            btd_id, g_saved_sink_id);
}

static void restore_sink(void) {
    if (!g_sink_switched) return;

    if (g_saved_sink_id >= 0) {
        set_default_sink(g_saved_sink_id);
        fprintf(stderr, "btd700d: restored default sink to node %d\n", g_saved_sink_id);
    }

    g_sink_switched = 0;
    g_saved_sink_id = -1;
}

static void on_event(const btd700_event_t* event, void* user_data) {
    (void)user_data;
    if (event->type != BTD700_EVENT_STATE_CHANGED || event->data_len < 1)
        return;

    btd700_dongle_state_t state = (btd700_dongle_state_t)event->data[0];
    fprintf(stderr, "btd700d: state -> %s\n", btd700_dongle_state_string(state));

    switch (state) {
    case BTD700_STATE_CONNECTED:
    case BTD700_STATE_STREAMING_AUDIO:
        switch_to_btd700();
        break;
    case BTD700_STATE_DISCONNECTED:
        restore_sink();
        break;
    default:
        break;
    }
}

static void interruptible_sleep(int seconds) {
    for (int i = 0; i < seconds * 10 && g_running; i++)
        usleep(100000);
}

int main(int argc, char* argv[]) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    btd700_driver_t* drv = NULL;
    btd700_error_t err = btd700_driver_create(&drv);
    if (err != BTD700_OK) {
        fprintf(stderr, "btd700d: create: %s\n", btd700_error_string(err));
        return 1;
    }

    while (g_running) {
        err = btd700_driver_connect(drv);
        if (err == BTD700_OK) break;
        fprintf(stderr, "btd700d: dongle not found, retrying in 5s...\n");
        interruptible_sleep(5);
    }

    if (!g_running) {
        btd700_driver_destroy(drv);
        return 0;
    }

    fprintf(stderr, "btd700d: connected to dongle\n");
    btd700_driver_set_event_callback(drv, on_event, NULL);

    btd700_dongle_state_t initial;
    if (btd700_driver_state(drv, &initial) == BTD700_OK) {
        if (initial == BTD700_STATE_CONNECTED ||
            initial == BTD700_STATE_STREAMING_AUDIO) {
            switch_to_btd700();
        }
    }

    // event loop
    int stable = 0;
    while (g_running) {
        err = btd700_driver_poll_events(drv, 200);
        if (err == BTD700_ERR_HID || err == BTD700_ERR_DEVICE_NOT_OPEN) {
            fprintf(stderr, "btd700d: HID error, reconnecting...\n");
            btd700_driver_disconnect(drv);
            if (stable) restore_sink();
            stable = 0;

            while (g_running) {
                err = btd700_driver_connect(drv);
                if (err == BTD700_OK) break;
                interruptible_sleep(5);
            }

            if (g_running) {
                fprintf(stderr, "btd700d: reconnected\n");
                btd700_driver_set_event_callback(drv, on_event, NULL);

                btd700_dongle_state_t state;
                if (btd700_driver_state(drv, &state) == BTD700_OK) {
                    if (state == BTD700_STATE_CONNECTED ||
                        state == BTD700_STATE_STREAMING_AUDIO) {
                        switch_to_btd700();
                    }
                }
            }
        } else {
            stable = 1;
        }
    }

    restore_sink();
    btd700_driver_disconnect(drv);
    btd700_driver_destroy(drv);
    fprintf(stderr, "btd700d: shutdown\n");
    return 0;
}
