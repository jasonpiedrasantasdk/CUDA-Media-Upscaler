#pragma once

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <QProcess>
#include <QString>
#include <QStringList>

namespace upscaler {

/// Result of running an external process.
struct ProcessResult {
    int exit_code = -1;
    std::string stdout_str;
    std::string stderr_str;
};

/// Simple logging helper (writes to stderr).
inline void log_info(const std::string& msg) {
    std::cerr << "[INFO] " << msg << std::endl;
}

inline void log_warn(const std::string& msg) {
    std::cerr << "[WARN] " << msg << std::endl;
}

inline void log_error(const std::string& msg) {
    std::cerr << "[ERROR] " << msg << std::endl;
}

/// Run an external command and capture its output.
/// @param args  The program followed by its arguments (args[0] is the executable).
/// @param timeout_ms  Maximum time to wait for the process (default 2 minutes).
/// @returns ProcessResult with captured stdout, stderr, and exit code.
inline ProcessResult run_command(const std::vector<std::string>& args,
                                 int timeout_ms = 120000) {
    if (args.empty()) {
        throw std::runtime_error("run_command: empty argument list");
    }

    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);

    QString program = QString::fromStdString(args[0]);
    QStringList qargs;
    for (size_t i = 1; i < args.size(); ++i) {
        qargs << QString::fromStdString(args[i]);
    }

    proc.start(program, qargs);
    if (!proc.waitForStarted(10000)) {
        throw std::runtime_error("run_command: failed to start process: " + args[0]);
    }

    if (!proc.waitForFinished(timeout_ms)) {
        proc.kill();
        proc.waitForFinished(5000);
        throw std::runtime_error("run_command: process timed out: " + args[0]);
    }

    ProcessResult result;
    result.exit_code  = proc.exitCode();
    result.stdout_str = proc.readAllStandardOutput().toStdString();
    result.stderr_str = proc.readAllStandardError().toStdString();
    return result;
}

/// Check whether an executable is available on PATH.
inline bool is_executable_available(const std::string& name) {
    try {
        auto r = run_command({"which", name}, 5000);
        return r.exit_code == 0;
    } catch (...) {
        return false;
    }
}

} // namespace upscaler
