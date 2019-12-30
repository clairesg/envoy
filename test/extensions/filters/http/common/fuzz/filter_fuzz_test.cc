#include <chrono>
#include <memory>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/config/filter/http/buffer/v2/buffer.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"

#include "common/config/utility.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/buffer/buffer_filter.h"
#include "extensions/filters/http/well_known_names.h"

#include "test/config/utility.h"
#include "test/extensions/filters/http/common/fuzz/decoder_filter_fuzz.h"
#include "test/extensions/filters/http/common/fuzz/filter_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

class UberFilterFuzzer {
public:
  UberFilterFuzzer() {
    ON_CALL(filter_callback_, addStreamDecoderFilter(_))
        .WillByDefault(
            Invoke([&](std::shared_ptr<Envoy::Http::StreamDecoderFilter> filter) -> void {
              filter_ = filter;
              filter_->setDecoderFilterCallbacks(callbacks_);
            }));
  }

  // This executes the methods to be fuzzed.
  void decode(Http::StreamDecoderFilter* filter, const test::fuzz::HttpData& data) {
    ENVOY_LOG_MISC(info, "Decoding {} with filter", data.DebugString());
    bool end_stream = false;

    Http::TestHeaderMapImpl headers = Fuzz::fromHeaders(data.headers());
    if (data.data().size() == 0 && !data.has_trailers()) {
      end_stream = true;
    }
    filter->decodeHeaders(headers, end_stream);

    for (int i = 0; i < data.data().size(); i++) {
      if (i == data.data().size() - 1 && !data.has_trailers()) {
        end_stream = true;
      }
      Buffer::OwnedImpl buffer(data.data().Get(i));
      filter->decodeData(buffer, end_stream);
    }

    if (data.has_trailers()) {
      Http::TestHeaderMapImpl trailers = Fuzz::fromHeaders(data.trailers());
      filter->decodeTrailers(trailers);
    }
  }

  // This creates the filter from the proto and runs decode.
  void
  fuzz(const envoy::config::filter::network::http_connection_manager::v2::HttpFilter& proto_config,
       const test::fuzz::HttpData& data) {
    // TODO: Dissociate filter creation from active stream.
    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
            proto_config.name());
    ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
        proto_config, Envoy::ProtobufMessage::getNullValidationVisitor(), factory);
    Http::FilterFactoryCb cb =
        factory.createFilterFactoryFromProto(*message, "fuzz", factory_context_);

    // Set the filter_ and execute the code to  be fuzzed.
    cb(filter_callback_);
    decode(filter_.get(), data);
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback_;
  std::shared_ptr<Http::StreamDecoderFilter> filter_;
};

DEFINE_PROTO_FUZZER(const test::extensions::filters::http::FilterFuzzTestCase& input) {
  // TODO: Hard-coded right now. Replace with pulling from a directory.
  const std::string config_string =
      R"EOF(
name: envoy.buffer
typed_config:
    "@type": type.googleapis.com/envoy.config.filter.http.buffer.v2.Buffer
    max_request_bytes : 5242880
)EOF";
  // Create proto_config from the string.
  envoy::config::filter::network::http_connection_manager::v2::HttpFilter proto_config;
  TestUtility::loadFromYaml(config_string, proto_config);

  // Fuzz filter.
  static UberFilterFuzzer fuzzer;
  fuzzer.fuzz(proto_config, input.data());
}

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
