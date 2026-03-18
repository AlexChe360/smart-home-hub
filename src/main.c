//
//  main.c
//  smart_home
//
//  Created by  Alexey on 12.03.2026.
//

#include "hub_core.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define DEFAULT_CONFIG "/etc/smarthome/hub.conf"
#define VERSION        "1.0.0"

static hub_core_t g_hub;

static void signal_handler(int sig)
{
    fprintf(stderr, "\n[main] Signal %d, shutting down...\n", sig);
    hub_stop(&g_hub);
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "SmartHome Hub Daemon v%s (C11)\n"
        "Usage: %s [OPTIONS]\n\n"
        "  -c PATH   Config file (default: %s)\n"
        "  -d        Debug mode\n"
        "  -h        Help\n"
        "  -v        Version\n",
        VERSION, prog, DEFAULT_CONFIG);
}

int main(int argc, char *argv[])
{
    const char *config_path = DEFAULT_CONFIG;
    int debug = 0;

    int opt;
    while ((opt = getopt(argc, argv, "c:dhv")) != -1) {
        switch (opt) {
        case 'c': config_path = optarg; break;
        case 'd': debug = 1; break;
        case 'v': printf("smarthome-hub v%s\n", VERSION); return 0;
        case 'h': default: usage(argv[0]); return opt == 'h' ? 0 : 1;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    printf("=================================\n");
    printf("  SmartHome Hub Daemon v%s\n", VERSION);
    printf("  Config: %s\n", config_path);
    printf("=================================\n");

    if (hub_init(&g_hub, config_path) < 0) {
        fprintf(stderr, "[main] Init failed\n");
        return 1;
    }

    if (debug) {
        g_hub.config.log_level = LOG_LVL_DEBUG;
        logger_set_level(LOG_LVL_DEBUG);
    }

    int rc = hub_run(&g_hub);
    hub_destroy(&g_hub);

    printf("[main] Exited with code %d\n", rc);
    return rc;
}
