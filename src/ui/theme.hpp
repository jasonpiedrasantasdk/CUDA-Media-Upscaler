#pragma once

#include <QString>

namespace upscaler {

struct Theme {
    // Backgrounds: darkest -> lightest
    static constexpr const char* BG_DARK    = "#0d1117";
    static constexpr const char* BG_MEDIUM  = "#161b22";
    static constexpr const char* BG_LIGHT   = "#21262d";
    static constexpr const char* BG_WIDGET  = "#30363d";

    // Foregrounds
    static constexpr const char* FG_PRIMARY   = "#e6edf3";
    static constexpr const char* FG_SECONDARY = "#8b949e";
    static constexpr const char* FG_VALUE     = "#79c0ff";

    // Accents
    static constexpr const char* ACCENT       = "#1f6feb";
    static constexpr const char* ACCENT_HOVER = "#388bfd";
    static constexpr const char* SUCCESS      = "#3fb950";
    static constexpr const char* WARNING      = "#d29922";
    static constexpr const char* ERROR_COLOR  = "#f85149";
    static constexpr const char* BORDER       = "#30363d";

    // Log colors (against BG_MEDIUM)
    static constexpr const char* LOG_INFO   = "#3fb950";
    static constexpr const char* LOG_DEBUG  = "#8b949e";
    static constexpr const char* LOG_WARN   = "#d29922";
    static constexpr const char* LOG_ERROR  = "#f85149";
    static constexpr const char* LOG_HEADER = "#79c0ff";

    /// Generate complete Qt stylesheet for the dark theme.
    static QString stylesheet();
};

} // namespace upscaler
