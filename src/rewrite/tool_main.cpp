#include "rewrite/platform/device.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') quoted += "'\\''";
        else quoted.push_back(ch);
    }
    quoted.push_back('\'');
    return quoted;
}

void print_probe_shell() {
    const rewrite_device_info_t info = rewrite_probe_device();
    std::printf("SKEETS_REWRITE_PRODUCT_ID=%s\n", shell_quote(info.product_id).c_str());
    std::printf("SKEETS_REWRITE_PRODUCT_NAME=%s\n", shell_quote(info.product_name).c_str());
    std::printf("SKEETS_REWRITE_CODENAME=%s\n", shell_quote(info.codename).c_str());
    std::printf("SKEETS_REWRITE_PLATFORM=%s\n", shell_quote(info.platform).c_str());
    std::printf("SKEETS_REWRITE_WIFI_MODULE=%s\n", shell_quote(info.wifi_module).c_str());
    std::printf("SKEETS_REWRITE_INTERFACE=%s\n", shell_quote(info.interface_name).c_str());
    std::printf("SKEETS_REWRITE_BATTERY_SYSFS=%s\n", shell_quote(info.battery_sysfs).c_str());
    std::printf("SKEETS_REWRITE_TOUCH_PROTOCOL=%s\n", shell_quote(info.touch_protocol).c_str());
    std::printf("SKEETS_REWRITE_IS_MTK=%s\n", info.is_mtk ? "1" : "0");
    std::printf("SKEETS_REWRITE_IS_SUNXI=%s\n", info.is_sunxi ? "1" : "0");
    std::printf("SKEETS_REWRITE_IS_COLOR=%s\n", info.is_color ? "1" : "0");
    std::printf("SKEETS_REWRITE_IS_SMP=%s\n", info.is_smp ? "1" : "0");
}

int do_ntx_io(const char* command_arg, const char* value_arg) {
    const long command = std::strtol(command_arg, nullptr, 10);
    const long value = std::strtol(value_arg, nullptr, 10);

    const int fd = open("/dev/ntx_io", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd == -1) {
        std::perror("open(/dev/ntx_io)");
        return 1;
    }

    const int result = ioctl(fd, command, static_cast<int>(value));
    if (result != 0) {
        std::perror("ioctl(/dev/ntx_io)");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}

void print_usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s probe-shell | ntx-io <cmd> <arg>\n",
                 argv0 ? argv0 : "sKeets-rewrite-tool");
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argc > 0 ? argv[0] : nullptr);
        return 64;
    }

    const std::string command = argv[1];
    if (command == "probe-shell") {
        print_probe_shell();
        return 0;
    }

    if (command == "ntx-io") {
        if (argc != 4) {
            print_usage(argv[0]);
            return 64;
        }
        return do_ntx_io(argv[2], argv[3]);
    }

    print_usage(argv[0]);
    return 64;
}