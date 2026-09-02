#include "opennow/ca_bundle.hpp"
#include "opennow/logger.hpp"

#include <cstdio>
#include <cstddef>

namespace opennow {

const char* ca_bundle_path() {
    static const char* selected = NULL;
    if (selected) return selected;
    static const char* const candidates[] = {
#ifdef OPENNOW_XDK
        "game:\\cacert.pem",
#endif
        "cacert.pem",
        "uda:/OpenNOW-Xenon/cacert.pem",
        "uda:/cacert.pem",
        "sda:/OpenNOW-Xenon/cacert.pem",
        "sda:/cacert.pem"
    };
    for (std::size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        const char* path = candidates[i];
        FILE* file = std::fopen(path, "rb");
        if (file) {
            std::fclose(file);
            selected = path;
            ON_LOGI("tls", "CA bundle selected path=%s", selected);
            return selected;
        }
    }
    // Preserve curl's useful file-open error when no candidate exists.
    selected = candidates[0];
    ON_LOGE("tls", "CA bundle not found; curl will attempt path=%s", selected);
    return selected;
}

}
