/* btd700ctl - command line control for the Sennheiser BTD 700 dongle
 *
 * usage: btd700ctl <command>
 *   status       print the dongle state as a single token
 *   info         print a human readable summary
 *   connect      ask the dongle to connect to its known headset
 *   disconnect   ask the dongle to drop the headset connection
 *   toggle       connect if disconnected, disconnect otherwise
 *   monitor      print the state on start and on every change (never exits);
 *                prints "absent" while the dongle is unplugged
 *
 * state tokens: absent, none, disconnected, connected, streaming-audio,
 *               streaming-voice, unknown
 */

#define _DEFAULT_SOURCE
#include <btd700/btd700_c.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static const char* state_token(btd700_dongle_state_t state) {
    switch (state) {
        case BTD700_STATE_NONE:            return "none";
        case BTD700_STATE_DISCONNECTED:    return "disconnected";
        case BTD700_STATE_CONNECTED:       return "connected";
        case BTD700_STATE_STREAMING_AUDIO: return "streaming-audio";
        case BTD700_STATE_STREAMING_VOICE: return "streaming-voice";
    }
    return "unknown";
}

static int is_linked(btd700_dongle_state_t state) {
    return state == BTD700_STATE_CONNECTED ||
           state == BTD700_STATE_STREAMING_AUDIO ||
           state == BTD700_STATE_STREAMING_VOICE;
}

static void usage(FILE* out) {
    fprintf(out,
        "usage: btd700ctl <command>\n"
        "\n"
        "commands:\n"
        "  status       print the dongle state (absent, disconnected, connected, ...)\n"
        "  info         print a human readable summary\n"
        "  connect      connect the dongle to its known headset\n"
        "  disconnect   disconnect the headset from the dongle\n"
        "  toggle       connect if disconnected, disconnect otherwise\n"
        "  monitor      print the state now and on every change\n");
}

static void emit(const char* token) {
    printf("%s\n", token);
    fflush(stdout);
}

static void on_monitor_event(const btd700_event_t* event, void* user_data) {
    (void)user_data;
    if (event->type == BTD700_EVENT_STATE_CHANGED && event->data_len >= 1)
        emit(state_token((btd700_dongle_state_t)event->data[0]));
}

static void sleep_interruptible(int ms) {
    for (int i = 0; i < ms / 100 && g_running; i++)
        usleep(100000);
}

static int cmd_monitor(btd700_driver_t* drv) {
    const char* last = NULL;

    while (g_running) {
        if (btd700_driver_connect(drv) != BTD700_OK) {
            if (!last || strcmp(last, "absent") != 0) emit("absent");
            last = "absent";
            sleep_interruptible(2000);
            continue;
        }

        btd700_dongle_state_t state;
        if (btd700_driver_state(drv, &state) != BTD700_OK) {
            btd700_driver_disconnect(drv);
            sleep_interruptible(1000);
            continue;
        }
        last = state_token(state);
        emit(last);

        btd700_driver_set_event_callback(drv, on_monitor_event, NULL);
        while (g_running) {
            btd700_error_t err = btd700_driver_poll_events(drv, 500);
            if (err == BTD700_ERR_HID || err == BTD700_ERR_DEVICE_NOT_OPEN) break;
        }
        btd700_driver_set_event_callback(drv, NULL, NULL);
        btd700_driver_disconnect(drv);
        last = NULL;
    }
    return 0;
}

static int cmd_info(btd700_driver_t* drv) {
    btd700_device_info_t info;
    btd700_firmware_version_t fw;
    btd700_dongle_state_t state;
    btd700_audio_config_t cfg;
    uint16_t codec = 0;

    if (btd700_driver_device_info(drv, &info) == BTD700_OK)
        printf("Device     : %s %s\n", info.manufacturer, info.product);
    if (btd700_driver_firmware_version(drv, &fw) == BTD700_OK)
        printf("Firmware   : %u.%u.%u\n", fw.major, fw.minor, fw.build);
    if (btd700_driver_state(drv, &state) == BTD700_OK)
        printf("State      : %s\n", btd700_dongle_state_string(state));
    if (btd700_driver_audio_config(drv, &cfg) == BTD700_OK) {
        printf("Audio mode : %s\n", btd700_audio_mode_string(cfg.mode));
        printf("Transport  : %s\n", btd700_transport_mode_string(cfg.transport));
    }
    if (btd700_driver_active_codec(drv, &codec) == BTD700_OK) {
        printf("Codec      :");
        int any = 0;
        for (int i = 0; i <= BTD700_CODEC_LC3; i++) {
            if (codec & (1 << i)) {
                printf(" %s", btd700_codec_string((btd700_codec_t)i));
                any = 1;
            }
        }
        printf("%s\n", any ? "" : " none");
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        usage(stderr);
        return 2;
    }
    const char* cmd = argv[1];
    if (!strcmp(cmd, "-h") || !strcmp(cmd, "--help") || !strcmp(cmd, "help")) {
        usage(stdout);
        return 0;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    btd700_driver_t* drv = NULL;
    btd700_error_t err = btd700_driver_create(&drv);
    if (err != BTD700_OK) {
        fprintf(stderr, "btd700ctl: %s\n", btd700_error_string(err));
        return 1;
    }

    int rc = 0;

    if (!strcmp(cmd, "monitor")) {
        rc = cmd_monitor(drv);
        btd700_driver_destroy(drv);
        return rc;
    }

    err = btd700_driver_connect(drv);
    if (err != BTD700_OK) {
        if (!strcmp(cmd, "status")) {
            emit("absent");
        } else {
            fprintf(stderr, "btd700ctl: dongle not found\n");
        }
        btd700_driver_destroy(drv);
        return 1;
    }

    btd700_dongle_state_t state = BTD700_STATE_NONE;

    if (!strcmp(cmd, "status")) {
        err = btd700_driver_state(drv, &state);
        if (err == BTD700_OK) emit(state_token(state));
    } else if (!strcmp(cmd, "info")) {
        rc = cmd_info(drv);
    } else if (!strcmp(cmd, "connect")) {
        err = btd700_driver_trigger_connect(drv);
    } else if (!strcmp(cmd, "disconnect")) {
        err = btd700_driver_trigger_disconnect(drv);
    } else if (!strcmp(cmd, "toggle")) {
        err = btd700_driver_state(drv, &state);
        if (err == BTD700_OK) {
            err = is_linked(state) ? btd700_driver_trigger_disconnect(drv)
                                   : btd700_driver_trigger_connect(drv);
        }
    } else {
        usage(stderr);
        rc = 2;
    }

    if (err != BTD700_OK) {
        fprintf(stderr, "btd700ctl: %s: %s\n", cmd, btd700_error_string(err));
        rc = 1;
    }

    btd700_driver_destroy(drv);
    return rc;
}
