#include "common/config.hpp"
#include "common/logger.hpp"
#include "metadata/metadata_store.hpp"
#include "metadata/metadata_service.hpp"
#include "coordinator/coordinator_service.hpp"
#include "network/grpc_util.hpp"
#include <csignal>
#include <atomic>
#include <thread>

static std::atomic<bool> g_running{true};
void signal_handler(int) { g_running = false; }

int main(int argc, char** argv) {
    dse::init_logger("metadata-service");
    auto& cfg = dse::Config::instance();

    std::string config_path;
    std::string listen_addr = "0.0.0.0:50051";
    std::vector<std::string> coordinator_peers;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value_after = [&](const std::string& key) -> std::string {
            if (arg.rfind(key + "=", 0) == 0) return arg.substr(key.size() + 1);
            return {};
        };
        if (arg == "--config" && i + 1 < argc) config_path = argv[++i];
        else if (arg == "--address" && i + 1 < argc) listen_addr = argv[++i];
        else if (auto v = value_after("--address"); !v.empty()) listen_addr = v;
        else if (arg == "--peer" && i + 1 < argc) coordinator_peers.push_back(argv[++i]);
        else if (auto v = value_after("--peer"); !v.empty()) coordinator_peers.push_back(v);
    }

    if (!config_path.empty()) cfg.load_from_file(config_path);
    cfg.load_from_env();
    cfg.set_listen_address(listen_addr);

    auto store = std::make_shared<dse::MetadataStore>(cfg.metadata_persist_path());
    dse::MetadataServiceImpl metadata_service(store);

    std::string coord_id = "coordinator-1";
    dse::CoordinatorServiceImpl coordinator(coord_id, coordinator_peers, store);
    coordinator.start();

    auto server = dse::build_server(listen_addr, {&metadata_service, &coordinator});
    if (!server) {
        spdlog::error("Failed to start metadata service on {}", listen_addr);
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    spdlog::info("Metadata service listening on {}", listen_addr);
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    coordinator.stop();
    server->Shutdown();
    store->persist();
    spdlog::info("Metadata service stopped");
    return 0;
}
