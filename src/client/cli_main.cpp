#include "common/config.hpp"
#include "common/logger.hpp"
#include "client/storage_client.hpp"
#include <iostream>
#include <iomanip>

void print_usage() {
    std::cout << "Distributed Storage Engine CLI\n\n"
              << "Usage:\n"
              << "  storage put <file>              Upload a file\n"
              << "  storage get <file> [output]     Download a file\n"
              << "  storage delete <file>           Delete a file\n"
              << "  storage list [prefix]           List objects\n"
              << "  storage head <file>             Get object metadata\n"
              << "\nOptions:\n"
              << "  --metadata <host:port>          Metadata service address (default: localhost:50051)\n";
}

int main(int argc, char** argv) {
    dse::init_logger("storage-cli", spdlog::level::warn);

    std::string metadata_addr = "localhost:50051";
    std::vector<std::string> args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--metadata" && i + 1 < argc) metadata_addr = argv[++i];
        else if (arg == "--help" || arg == "-h") { print_usage(); return 0; }
        else args.push_back(arg);
    }

    if (args.empty()) { print_usage(); return 1; }

    dse::StorageClient client(metadata_addr);
    const std::string& cmd = args[0];

    if (cmd == "put" && args.size() >= 2) {
        auto result = client.put_file(args[1]);
        if (result.success) {
            std::cout << "Uploaded: " << args[1] << " (id=" << result.object_id << ")\n";
            return 0;
        }
        std::cerr << "Upload failed: " << result.message << "\n";
        return 1;
    }

    if (cmd == "get" && args.size() >= 2) {
        std::string output = args.size() >= 3 ? args[2] : ("./" + args[1]);
        auto result = client.get_file(args[1], output);
        if (result.success) {
            std::cout << "Downloaded: " << args[1] << " -> " << output
                      << " (" << result.data.size() << " bytes)\n";
            return 0;
        }
        std::cerr << "Download failed: " << result.message << "\n";
        return 1;
    }

    if (cmd == "delete" && args.size() >= 2) {
        if (client.delete_object(args[1])) {
            std::cout << "Deleted: " << args[1] << "\n";
            return 0;
        }
        std::cerr << "Delete failed\n";
        return 1;
    }

    if (cmd == "list") {
        std::string prefix = args.size() >= 2 ? args[1] : "";
        auto objects = client.list(prefix);
        std::cout << std::left << std::setw(40) << "FILENAME"
                  << std::setw(15) << "SIZE" << "OWNER\n";
        std::cout << std::string(70, '-') << "\n";
        for (const auto& obj : objects) {
            std::cout << std::left << std::setw(40) << obj.filename
                      << std::setw(15) << obj.size << obj.owner << "\n";
        }
        std::cout << "\nTotal: " << objects.size() << " objects\n";
        return 0;
    }

    if (cmd == "head" && args.size() >= 2) {
        auto info = client.head(args[1]);
        if (!info) {
            std::cerr << "Not found: " << args[1] << "\n";
            return 1;
        }
        std::cout << "Filename:    " << info->filename << "\n"
                  << "Object ID:   " << info->object_id << "\n"
                  << "Size:        " << info->size << " bytes\n"
                  << "Replicas:    " << info->replication_factor << "\n"
                  << "Version:     " << info->version << "\n"
                  << "Owner:       " << info->owner << "\n";
        return 0;
    }

    print_usage();
    return 1;
}
