#include "platform/framebuffer.h"
#include "platform/input.h"
#include "platform/network.h"
#include "platform/power.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <string>

namespace {

void log_framebuffer_info(const rewrite_framebuffer_info_t& info) {
    std::fprintf(stderr, "rewrite diag: framebuffer.screen=%dx%d\n", info.screen_width, info.screen_height);
    std::fprintf(stderr, "rewrite diag: framebuffer.view=%dx%d\n", info.view_width, info.view_height);
    std::fprintf(stderr, "rewrite diag: framebuffer.font=%dx%d\n", info.font_width, info.font_height);
    std::fprintf(stderr,
                 "rewrite diag: framebuffer.capabilities full=%d partial=%d fast=%d gc16_partial=%d wait=%d color=%d mtk=%d\n",
                 info.supports_full_refresh ? 1 : 0,
                 info.supports_partial_refresh ? 1 : 0,
                 info.supports_fast_refresh ? 1 : 0,
                 info.supports_grayscale_partial_refresh ? 1 : 0,
                 info.supports_wait_for_complete ? 1 : 0,
                 info.is_color_panel ? 1 : 0,
                 info.is_mtk_panel ? 1 : 0);
}

int run_framebuffer_diag() {
    std::fprintf(stderr, "rewrite diag: framebuffer.begin\n");

    rewrite_framebuffer_t framebuffer;
    std::string error_message;
    if (!rewrite_framebuffer_open(framebuffer, &error_message)) {
        std::fprintf(stderr, "rewrite diag: framebuffer.open_failed=%s\n", error_message.c_str());
        return 2;
    }

    log_framebuffer_info(framebuffer.info);

    const int margin = std::max(24, framebuffer.info.screen_width / 18);
    const int block_gap = std::max(12, margin / 3);
    const int block_width = std::max(48, (framebuffer.info.screen_width - (margin * 2) - (block_gap * 2)) / 3);
    const int block_height = std::max(96, framebuffer.info.screen_height / 6);
    const int top = std::max(48, framebuffer.info.screen_height / 8);

    const rewrite_rect_t header{margin, margin, framebuffer.info.screen_width - (margin * 2), 24};
    const rewrite_rect_t block_one{margin, top, block_width, block_height};
    const rewrite_rect_t block_two{margin + block_width + block_gap, top, block_width, block_height};
    const rewrite_rect_t block_three{margin + ((block_width + block_gap) * 2), top, block_width, block_height};
    const rewrite_rect_t footer{margin, top + block_height + block_gap, framebuffer.info.screen_width - (margin * 2), 20};

    rewrite_framebuffer_clear(framebuffer, 0xFF);
    if (!rewrite_framebuffer_refresh(framebuffer, rewrite_refresh_mode_t::full, {}, true, &error_message)) {
        std::fprintf(stderr, "rewrite diag: framebuffer.full_refresh_failed=%s\n", error_message.c_str());
        rewrite_framebuffer_close(framebuffer);
        return 3;
    }

    rewrite_framebuffer_fill_rect(framebuffer, header, 0x00);
    rewrite_framebuffer_fill_rect(framebuffer, block_one, 0x20);
    rewrite_framebuffer_fill_rect(framebuffer, block_two, 0x80);
    rewrite_framebuffer_fill_rect(framebuffer, block_three, 0xD0);
    rewrite_framebuffer_fill_rect(framebuffer, footer, 0x00);
    rewrite_framebuffer_mark_dirty(framebuffer, header);
    rewrite_framebuffer_mark_dirty(framebuffer, block_one);
    rewrite_framebuffer_mark_dirty(framebuffer, block_two);
    rewrite_framebuffer_mark_dirty(framebuffer, block_three);
    rewrite_framebuffer_mark_dirty(framebuffer, footer);

    if (!rewrite_framebuffer_flush(framebuffer,
                                   rewrite_refresh_mode_t::grayscale_partial,
                                   true,
                                   &error_message)) {
        std::fprintf(stderr, "rewrite diag: framebuffer.partial_refresh_failed=%s\n", error_message.c_str());
        rewrite_framebuffer_close(framebuffer);
        return 4;
    }

    rewrite_framebuffer_close(framebuffer);
    std::fprintf(stderr, "rewrite diag: framebuffer.done\n");
    return 0;
}

rewrite_input_protocol_t detect_input_protocol() {
    const QByteArray protocol = qEnvironmentVariable("SKEETS_REWRITE_TOUCH_PROTOCOL").toUtf8();
    if (protocol == "snow") return rewrite_input_protocol_t::snow;
    if (!protocol.isEmpty()) return rewrite_input_protocol_t::standard_multitouch;
    return rewrite_input_protocol_t::unknown;
}

int run_input_diag() {
    std::fprintf(stderr, "rewrite diag: input.begin\n");

    rewrite_input_t input;
    input.debug_raw_events = true;
    input.raw_event_log_budget = 64;
    std::string error_message;
    if (!rewrite_input_open(input, 1072, 1448, detect_input_protocol(), &error_message)) {
        std::fprintf(stderr, "rewrite diag: input.open_failed=%s\n", error_message.c_str());
        return 5;
    }

    std::fprintf(stderr, "rewrite diag: input.protocol=%s\n", rewrite_input_protocol_name(input.protocol));
    std::fprintf(stderr, "rewrite diag: input.device_count=%zu\n", input.devices.size());
    for (size_t index = 0; index < input.devices.size(); ++index) {
        const auto& device = input.devices[index];
        std::fprintf(stderr,
                     "rewrite diag: input.device[%zu]=path=%s name=%s touch=%d key=%d slot=%d tracking=%d axes=%d/%d swap=%d range_x=%d..%d range_y=%d..%d\n",
                     index,
                     device.path.c_str(),
                     device.name.c_str(),
                     device.is_touch_device ? 1 : 0,
                     device.is_key_device ? 1 : 0,
                     device.has_mt_slot ? 1 : 0,
                     device.has_tracking_id ? 1 : 0,
                     device.x_code,
                     device.y_code,
                     device.swap_axes ? 1 : 0,
                     device.x_min,
                     device.x_max,
                     device.y_min,
                     device.y_max);
    }
    std::fprintf(stderr, "rewrite diag: input.touch_device_index=%d\n", input.touch_device_index);
    std::fprintf(stderr, "rewrite diag: input.key_device_index=%d\n", input.key_device_index);
    std::fprintf(stderr, "rewrite diag: input.listen_window_ms=8000\n");

    int captured_events = 0;
    for (int iteration = 0; iteration < 16; ++iteration) {
        rewrite_input_event_t event;
        if (!rewrite_input_poll(input, event, 500, &error_message)) {
            continue;
        }

        ++captured_events;
        switch (event.type) {
        case rewrite_input_event_type_t::touch_down:
            std::fprintf(stderr, "rewrite diag: input.event=touch_down slot=%d id=%d x=%d y=%d src=%s\n",
                         event.slot, event.tracking_id, event.x, event.y, event.source_path.c_str());
            break;
        case rewrite_input_event_type_t::touch_move:
            std::fprintf(stderr, "rewrite diag: input.event=touch_move slot=%d id=%d x=%d y=%d src=%s\n",
                         event.slot, event.tracking_id, event.x, event.y, event.source_path.c_str());
            break;
        case rewrite_input_event_type_t::touch_up:
            std::fprintf(stderr, "rewrite diag: input.event=touch_up slot=%d id=%d x=%d y=%d src=%s\n",
                         event.slot, event.tracking_id, event.x, event.y, event.source_path.c_str());
            break;
        case rewrite_input_event_type_t::key_press:
            std::fprintf(stderr, "rewrite diag: input.event=key_press code=%d src=%s\n",
                         event.key_code, event.source_path.c_str());
            break;
        case rewrite_input_event_type_t::none:
        default:
            break;
        }
    }

    std::fprintf(stderr, "rewrite diag: input.captured_events=%d\n", captured_events);
    rewrite_input_close(input);
    std::fprintf(stderr, "rewrite diag: input.done\n");
    return 0;
}

int run_power_diag() {
    std::fprintf(stderr, "rewrite diag: power.begin\n");
    const rewrite_power_info_t power = rewrite_probe_power();
    std::fprintf(stderr, "rewrite diag: power.battery_sysfs=%s\n", power.battery_sysfs.c_str());
    std::fprintf(stderr, "rewrite diag: power.battery_present=%d\n", power.battery_present ? 1 : 0);
    std::fprintf(stderr, "rewrite diag: power.capacity_percent=%d\n", power.capacity_percent);
    std::fprintf(stderr, "rewrite diag: power.status=%s\n", power.status.empty() ? "<unknown>" : power.status.c_str());
    std::fprintf(stderr, "rewrite diag: power.is_charging=%d\n", power.is_charging ? 1 : 0);
    std::fprintf(stderr, "rewrite diag: power.is_charged=%d\n", power.is_charged ? 1 : 0);
    std::fprintf(stderr, "rewrite diag: power.can_standby=%d\n", power.can_standby ? 1 : 0);
    std::fprintf(stderr, "rewrite diag: power.can_suspend=%d\n", power.can_suspend ? 1 : 0);
    std::fprintf(stderr, "rewrite diag: power.suspend_safe=%d\n", power.suspend_safe ? 1 : 0);
    std::fprintf(stderr, "rewrite diag: power.suspend_reason=%s\n", power.suspend_reason.c_str());
    std::fprintf(stderr, "rewrite diag: power.done\n");
    return 0;
}

int run_network_diag() {
    std::fprintf(stderr, "rewrite diag: network.begin\n");
    const rewrite_network_info_t network = rewrite_probe_network();
    std::fprintf(stderr, "rewrite diag: network.interface=%s\n", network.interface_name.c_str());
    std::fprintf(stderr, "rewrite diag: network.radio_present=%d\n", network.radio_present ? 1 : 0);
    std::fprintf(stderr, "rewrite diag: network.carrier_up=%d\n", network.carrier_up ? 1 : 0);
    std::fprintf(stderr, "rewrite diag: network.operstate_up=%d\n", network.operstate_up ? 1 : 0);
    std::fprintf(stderr, "rewrite diag: network.has_ipv4_address=%d\n", network.has_ipv4_address ? 1 : 0);
    std::fprintf(stderr, "rewrite diag: network.has_ipv6_address=%d\n", network.has_ipv6_address ? 1 : 0);
    for (size_t index = 0; index < network.addresses.size(); ++index) {
        std::fprintf(stderr, "rewrite diag: network.address[%zu]=%s\n", index, network.addresses[index].c_str());
    }
    std::fprintf(stderr, "rewrite diag: network.has_default_ipv4_route=%d\n", network.has_default_ipv4_route ? 1 : 0);
    std::fprintf(stderr, "rewrite diag: network.has_default_ipv6_route=%d\n", network.has_default_ipv6_route ? 1 : 0);
    std::fprintf(stderr, "rewrite diag: network.can_resolve_dns=%d\n", network.can_resolve_dns ? 1 : 0);
    std::fprintf(stderr, "rewrite diag: network.online=%d\n", network.online ? 1 : 0);
    std::fprintf(stderr, "rewrite diag: network.done\n");
    return 0;
}

int run_phase2_diag() {
    std::fprintf(stderr, "rewrite diag: phase2.begin\n");

    const int framebuffer_status = run_framebuffer_diag();
    if (framebuffer_status != 0) return framebuffer_status;

    const int input_status = run_input_diag();
    if (input_status != 0) return input_status;

    const int power_status = run_power_diag();
    if (power_status != 0) return power_status;

    const int network_status = run_network_diag();
    if (network_status != 0) return network_status;

    std::fprintf(stderr, "rewrite diag: phase2.done\n");
    return 0;
}

} // namespace

static void log_env(const char* key) {
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString value = env.value(QString::fromUtf8(key));
    std::fprintf(stderr, "rewrite diag: %s=%s\n", key, value.toUtf8().constData());
}

static void log_file_excerpt(const char* label, const QString& path) {
    QFile file(path);
    if (!file.exists()) {
        std::fprintf(stderr, "rewrite diag: %s missing: %s\n", label, path.toUtf8().constData());
        return;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "rewrite diag: %s unreadable: %s\n", label, path.toUtf8().constData());
        return;
    }
    const QByteArray data = file.read(512).trimmed();
    std::fprintf(stderr, "rewrite diag: %s=%s\n", label, data.constData());
}

static void log_exists(const char* label, const QString& path) {
    std::fprintf(stderr, "rewrite diag: %s=%s (%s)\n",
                 label,
                 path.toUtf8().constData(),
                 QFile::exists(path) ? "present" : "missing");
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QString rewriteDir = qEnvironmentVariable("SKEETS_REWRITE_DIR", "/mnt/onboard/.adds/sKeets");
    const QString scriptDir = rewriteDir + "/scripts/wifi";

    std::fprintf(stderr, "rewrite diag: process start\n");
    std::fprintf(stderr, "rewrite diag: argv[0]=%s\n", argc > 0 ? argv[0] : "<none>");
    std::fprintf(stderr, "rewrite diag: cwd=%s\n", QDir::currentPath().toUtf8().constData());
    std::fprintf(stderr, "rewrite diag: applicationDirPath=%s\n",
                 QCoreApplication::applicationDirPath().toUtf8().constData());
    std::fprintf(stderr, "rewrite diag: mode=%s\n",
                 qEnvironmentVariable("SKEETS_REWRITE_MODE").toUtf8().constData());

    log_env("LD_LIBRARY_PATH");
    log_env("QT_PLUGIN_PATH");
    log_env("SSL_CERT_FILE");
    log_env("LOCPATH");
    log_env("SKEETS_REWRITE_DIR");
    log_env("SKEETS_REWRITE_INTERFACE");
    log_env("SKEETS_REWRITE_PLATFORM");
    log_env("SKEETS_REWRITE_WIFI_MODULE");
    log_env("SKEETS_REWRITE_PRODUCT_ID");
    log_env("SKEETS_REWRITE_PRODUCT_NAME");
    log_env("SKEETS_REWRITE_CODENAME");
    log_env("SKEETS_REWRITE_BATTERY_SYSFS");
    log_env("SKEETS_REWRITE_TOUCH_PROTOCOL");
    log_env("SKEETS_REWRITE_IS_MTK");
    log_env("SKEETS_REWRITE_IS_SUNXI");
    log_env("SKEETS_REWRITE_IS_COLOR");
    log_env("SKEETS_REWRITE_IS_SMP");

    log_file_excerpt("kobo.version", "/mnt/onboard/.kobo/version");
    log_file_excerpt("rewrite.revision", rewriteDir + "/package-revision.txt");

    log_exists("rewrite.root", rewriteDir);
    log_exists("rewrite.script_dir", scriptDir);
    log_exists("rewrite.script.enable", scriptDir + "/enable-wifi.sh");
    log_exists("rewrite.script.disable", scriptDir + "/disable-wifi.sh");
    log_exists("rewrite.script.obtain_ip", scriptDir + "/obtain-ip.sh");
    log_exists("rewrite.script.release_ip", scriptDir + "/release-ip.sh");
    log_exists("rewrite.script.restore", scriptDir + "/restore-wifi-async.sh");

    if (qEnvironmentVariable("SKEETS_REWRITE_MODE") == QStringLiteral("fb-diag")) {
        const int framebuffer_status = run_framebuffer_diag();
        if (framebuffer_status != 0) return framebuffer_status;
    }

    if (qEnvironmentVariable("SKEETS_REWRITE_MODE") == QStringLiteral("input-diag")) {
        const int input_status = run_input_diag();
        if (input_status != 0) return input_status;
    }

    if (qEnvironmentVariable("SKEETS_REWRITE_MODE") == QStringLiteral("power-diag")) {
        const int power_status = run_power_diag();
        if (power_status != 0) return power_status;
    }

    if (qEnvironmentVariable("SKEETS_REWRITE_MODE") == QStringLiteral("network-diag")) {
        const int network_status = run_network_diag();
        if (network_status != 0) return network_status;
    }

    if (qEnvironmentVariable("SKEETS_REWRITE_MODE") == QStringLiteral("phase2-diag")) {
        const int phase2_status = run_phase2_diag();
        if (phase2_status != 0) return phase2_status;
    }

    std::fprintf(stderr, "rewrite diag: done\n");
    return 0;
}
