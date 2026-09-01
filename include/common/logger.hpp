#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <string>

namespace dse {

inline void init_logger(const std::string& name, spdlog::level::level_enum level = spdlog::level::info) {
    auto logger = spdlog::stdout_color_mt(name);
    logger->set_level(level);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
    spdlog::set_default_logger(logger);
}

}  // namespace dse
