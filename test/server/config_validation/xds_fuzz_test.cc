#include <fstream>

#include "test/fuzz/fuzz_runner.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/server/config_validation/xds_fuzz.pb.h"
#include "test/integration/fake_upstream.h"
#include "envoy/event/dispatcher.h"


namespace Envoy {
namespace Server {

const std::string config = R"EOF(
dynamic_resources:
  lds_config: {ads: {}}
  cds_config: {ads: {}}
  ads_config:
    api_type: GRPC
static_resources:
  clusters:
    name: dummy_cluster
    connect_timeout: { seconds: 5 }
    type: STATIC
    hosts:
      socket_address:
        address: 127.0.0.1
        port_value: 0
    lb_policy: ROUND_ROBIN
    http2_protocol_options: {}
admin:
  access_log_path: /dev/null
  address:
    socket_address:
      address: 127.0.0.1
      port_value: 0
)EOF";

class XdsFuzzConfig : BaseIntegrationTest {
public:
  XdsFuzzConfig()
      : BaseIntegrationTest(
            [](int) {
              return Network::Utility::parseInternetAddress(
                  Network::Test::getAnyAddressString(Network::Address::IpVersion::v4), 0);
            },
            Envoy::Network::Address::IpVersion::v4, config) {}

  envoy::api::v2::Listener buildListener(const std::string& name, const std::string& route_config,
                                         const std::string& stat_prefix = "ads_test") {
    return TestUtility::parseYaml<envoy::api::v2::Listener>(fmt::format(
        R"EOF(
      name: {}
      address:
        socket_address:
          address: {}
          port_value: 0
      filter_chains:
        filters:
        - name: envoy.http_connection_manager
          config:
            stat_prefix: {}
            codec_type: HTTP2
            rds:
              route_config_name: {}
              config_source: {{ ads: {{}} }}
            http_filters: [{{ name: envoy.router }}]
    )EOF",
        name, Network::Test::getLoopbackAddressString(version_), stat_prefix, route_config));
  }

  envoy::api::v2::Cluster buildCluster(const std::string& name) {
    return TestUtility::parseYaml<envoy::api::v2::Cluster>(fmt::format(R"EOF(
      name: {}
      connect_timeout: 5s
      type: EDS
      eds_cluster_config: {{ eds_config: {{ ads: {{}} }} }}
      lb_policy: ROUND_ROBIN
      http2_protocol_options: {{}}
    )EOF",
                                                                       name));
  }

    envoy::api::v2::RouteConfiguration buildRouteConfig(const std::string& name,
                                                      const std::string& cluster) {
    return TestUtility::parseYaml<envoy::api::v2::RouteConfiguration>(fmt::format(R"EOF(
      name: {}
      virtual_hosts:
      - name: integration
        domains: ["*"]
        routes:
        - match: {{ prefix: "/" }}
          route: {{ cluster: {} }}
    )EOF",
                                                                                  name, cluster));
  }

  void initialize() override {

      config_helper_.addConfigModifier(
        [this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
          auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
          auto* grpc_service = ads_config->add_grpc_services();
          grpc_service->mutable_envoy_grpc()->set_cluster_name("ads_cluster");
          auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
          ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
          ads_cluster->set_name("ads_cluster");
          //          auto* context = ads_cluster->mutable_tls_context();
          //auto* validation_context =
          //    context->mutable_common_tls_context()->mutable_validation_context();
          //validation_context->mutable_trusted_ca()->set_filename(
          //    TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
          //validation_context->add_verify_subject_alt_name("foo.lyft.com");
        });
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);

    BaseIntegrationTest::initialize();

    if (xds_stream_ == nullptr) {
      createXdsConnection();
      AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
      RELEASE_ASSERT(result, result.message());
      xds_stream_->startGrpcStream();
    }
  }

  void sendListenerDiscoveryResponse() {
    sendDiscoveryResponse<envoy::api::v2::Listener>(
        Config::TypeUrl::get().Listener, {buildListener("listener_0", "route_config_0")},
        {buildListener("listener_0", "route_config_0")}, {}, "1");
  }

  void sendClusterDiscoveryResponse() {
    sendDiscoveryResponse<envoy::api::v2::Cluster>(Config::TypeUrl::get().Cluster,
                                                   {buildCluster("cluster_0")},
                                                   {buildCluster("cluster_0")}, {}, "1");
  }
};

DEFINE_PROTO_FUZZER(const test::server::config_validation::XdsTestCase& input) {

  XdsFuzzConfig test;
  // Start ADS over gRPC
  test.initialize();
       ENVOY_LOG_MISC(trace, "aa {}", "");

  test.sendClusterDiscoveryResponse();
  ENVOY_LOG_MISC(trace, "Sent response: {}", "");
  // Go through actions
  for (const auto& action : input.actions()) {
    switch (action.action_selector_case()) {
      case test::server::config_validation::Action::kAddListener: {
    }
      case test::server::config_validation::Action::kAddRoute: {
    }
    default:
      break;
    }
  }

  // Verify listeners
}

}  // namespace Server
}  // namespace Envoy
