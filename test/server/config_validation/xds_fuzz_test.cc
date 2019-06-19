#include <fstream>

#include "test/fuzz/fuzz_runner.h"
#include "test/server/config_validation/xds_fuzz.pb.h"
#include "test/integration/fake_upstream.h"
#include "envoy/event/dispatcher.h"


namespace Envoy {
namespace Server {

const std::string config = R"EOF(

)EOF";

class XdsFuzzConfig {
 public:
  XdsFuzzConfig() {}

  void initializeAds() {
    xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
    xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    xds_stream_->startGrpcStream();
  }

 public:
  Event::DispatcherPtr dispatcher_;

 protected:
  FakeUpstream* xds_upstream_{};
  FakeHttpConnectionPtr xds_connection_;
  FakeStreamPtr xds_stream_;
};

DEFINE_PROTO_FUZZER(const test::server::config_validation::XdsTestCase& input) {
  XdsFuzzConfig test;
  // Start ADS over gRPC
  test.initializeAds();
  // Go through actions
  for (const auto& action : input.actions()) {
    switch (action.action_selector_case()) {
    case test::server::config_validation::Action::kListenerAdd: {
    }
    case test::server::config_validation::Action::kRouteAdd: {
    }
    default:
      break;
    }
  }

  // Verify listeners
}

}  // namespace Server
}  // namespace Envoy
