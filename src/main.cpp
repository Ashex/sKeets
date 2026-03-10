#include "app.h"
#include <QCoreApplication>
#include <csignal>
#include <cstdlib>
#include <cstdio>

static app_state_t* g_state = nullptr;

static void handle_signal(int) {
    if (g_state) g_state->running = false;
    QCoreApplication::quit();
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app_state_t state;
    g_state = &state;
    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);
    if (app_init(&state) != 0) {
        fprintf(stderr, "Failed to initialise application\n");
        return EXIT_FAILURE;
    }
    app_run(&state);
    app_shutdown(&state);
    return EXIT_SUCCESS;
}
