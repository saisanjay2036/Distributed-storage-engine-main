#pragma once

#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>

namespace dse {

std::shared_ptr<grpc::Channel> create_channel(const std::string& address);
std::unique_ptr<grpc::Server> build_server(const std::string& address,
                                           const std::vector<grpc::Service*>& services);

}  // namespace dse
