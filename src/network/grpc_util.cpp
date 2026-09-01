#include "network/grpc_util.hpp"

namespace dse {

std::shared_ptr<grpc::Channel> create_channel(const std::string& address) {
    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(64 * 1024 * 1024);
    args.SetMaxSendMessageSize(64 * 1024 * 1024);
    return grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), args);
}

std::unique_ptr<grpc::Server> build_server(const std::string& address,
                                           const std::vector<grpc::Service*>& services) {
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    for (auto* svc : services) {
        builder.RegisterService(svc);
    }
    builder.SetMaxReceiveMessageSize(64 * 1024 * 1024);
    builder.SetMaxSendMessageSize(64 * 1024 * 1024);
    return builder.BuildAndStart();
}

}  // namespace dse
