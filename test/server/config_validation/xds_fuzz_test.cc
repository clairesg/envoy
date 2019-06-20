#include <fstream>
#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/integration/integration.h"
#include "test/integration/utility.h"
#include "test/integration/http_integration.h"
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

class XdsFuzzTest : public HttpIntegrationTest {
public:
  XdsFuzzTest(Network::Address::IpVersion version)
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, version, config) {
    use_lds_ = false;
    create_xds_upstream_ = true;
    tls_xds_upstream_ = false;
  }

  void initialize() override {
    // Add ADS config with gRPC.
    // QUESTION: Can I add just add this to the config string?
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* ads_config = bootstrap.mutable_dynamic_resources()->mutable_ads_config();
      auto* grpc_service = ads_config->add_grpc_services();
      grpc_service->mutable_envoy_grpc()->set_cluster_name("ads_cluster");
      auto* ads_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ads_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ads_cluster->set_name("ads_cluster");
    });

    // createUpstream, createXdsUpstream, createEnvoy, and set initialized_ = true
    HttpIntegrationTest::initialize();

    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);
    if (xds_stream_ == nullptr) {
      createXdsConnection();
      AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
      RELEASE_ASSERT(result, result.message());
      xds_stream_->startGrpcStream();
    }
  }


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
            codec_type: HTTP1
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


  void removeListener();

  void updateListener();

  void addListener(const test::server::config_validation::AddListener& add_listener) {
    // Parse action info into a DiscoveryResponse.
    sendDiscoveryResponse<envoy::api::v2::Listener>(
        Config::TypeUrl::get().Listener,
        {buildListener(add_listener.name(), add_listener.route_config())},
        {buildListener(add_listener.name(), add_listener.route_config())}, {},
        add_listener.version());
  }

  void replay(const test::server::config_validation::XdsTestCase& input) {
    // QUESTION: sanity check -- doesn't this init after each test case input?
    // Maybe the upstream connection is only initialized if it's not set.. check...
    // Go through actions.
    initialize();

    for (const auto& action : input.actions()) {
      switch (action.action_selector_case()) {
      case test::server::config_validation::Action::kAddListener: {
        addListener(action.add_listener());
      }
      case test::server::config_validation::Action::kAddRoute: {
      }
      default:
        break;
      }
    }
    // Disconnect and close.
    close();
  }

  void close() {
    cleanUpXdsConnection();
    test_server_.reset();
    fake_upstreams_.clear();
  }

  const std::chrono::milliseconds max_wait_ms_{10};

  // Just for test
  void LogConfigDump() {
    auto message_ptr =
        test_server_->server().admin().getConfigTracker().getCallbacksMap().at("listeners")();
    ENVOY_LOG_MISC(debug, "CONFIG DUMP: {}",
                   dynamic_cast<const envoy::admin::v2alpha::ListenersConfigDump&>(*message_ptr)
                       .DebugString());
  }


};

DEFINE_PROTO_FUZZER(const test::server::config_validation::XdsTestCase& input) {
  RELEASE_ASSERT(!TestEnvironment::getIpVersionsForTest().empty(), "");
  const auto ip_version = TestEnvironment::getIpVersionsForTest()[0];
  XdsFuzzTest test(ip_version);

  test.replay(input);
}

}  // namespace Server
}  // namespace Envoy
