#include "app.h"
#include <QCoreApplication>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <clocale>

static app_state_t* g_state = nullptr;

static void handle_signal(int) {
    fprintf(stderr, "sKeets: received termination signal\n");
    if (g_state) g_state->running = false;
    QCoreApplication::quit();
}

int main(int argc, char* argv[]) {
    fprintf(stderr, "sKeets: process start\n");

    setenv("LC_ALL", "C.UTF-8", 1);
    setenv("LANG", "C.UTF-8", 1);
    if (!setlocale(LC_ALL, "C.UTF-8")) {
        setenv("LC_ALL", "C.utf8", 1);
        setenv("LANG", "C.utf8", 1);
        setlocale(LC_ALL, "C.utf8");
    }

    QCoreApplication app(argc, argv);
    app_state_t state;
    g_state = &state;
    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);
    if (app_init(&state) != 0) {
        fprintf(stderr, "sKeets: failed to initialise application\n");
        return EXIT_FAILURE;
    }
    fprintf(stderr, "sKeets: entering main loop\n");
    app_run(&state);
    fprintf(stderr, "sKeets: shutting down\n");
    app_shutdown(&state);
    fprintf(stderr, "sKeets: shutdown complete\n");
    return EXIT_SUCCESS;
}
