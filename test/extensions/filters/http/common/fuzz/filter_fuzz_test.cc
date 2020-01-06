#include <chrono>
#include <memory>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"

#include "common/config/utility.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/buffer/buffer_filter.h"
#include "extensions/filters/http/well_known_names.h"

#include "test/config/utility.h"
#include "test/extensions/filters/http/common/fuzz/filter_fuzz.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"

#include "libprotobuf_mutator/src/mutator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

class UberFilterFuzzer {
public:
  UberFilterFuzzer() {
    // Need to set for both a decoder filter and an encoder/decoder filter.
    ON_CALL(filter_callback_, addStreamDecoderFilter(_))
        .WillByDefault(
            Invoke([&](std::shared_ptr<Envoy::Http::StreamDecoderFilter> filter) -> void {
              filter_ = filter;
              filter_->setDecoderFilterCallbacks(callbacks_);
            }));
    ON_CALL(filter_callback_, addStreamFilter(_))
        .WillByDefault(
            Invoke([&](std::shared_ptr<Envoy::Http::StreamDecoderFilter> filter) -> void {
              filter_ = filter;
              filter_->setDecoderFilterCallbacks(callbacks_);
            }));
    callbacks_.stream_info_.protocol_ = Envoy::Http::Protocol::Http2;
  }

  // This executes the methods to be fuzzed.
  void decode(Http::StreamDecoderFilter* filter, const test::fuzz::HttpData& data) {
    bool end_stream = false;

    Http::TestHeaderMapImpl headers = Fuzz::fromHeaders(data.headers());
    if (data.data().size() == 0 && !data.has_trailers()) {
      end_stream = true;
    }
    ENVOY_LOG_MISC(debug, "Decoding headers: {} ", data.headers().DebugString());
    const auto& headersStatus = filter->decodeHeaders(headers, end_stream);
    if (headersStatus != Http::FilterHeadersStatus::Continue ||
        headersStatus != Http::FilterHeadersStatus::StopIteration) {
      return;
    }

    for (int i = 0; i < data.data().size(); i++) {
      if (i == data.data().size() - 1 && !data.has_trailers()) {
        end_stream = true;
      }
      Buffer::OwnedImpl buffer(data.data().Get(i));
      ENVOY_LOG_MISC(debug, "Decoding data: {} ", buffer.toString());
      if (filter->decodeData(buffer, end_stream) != Http::FilterDataStatus::Continue) {
      }
    }

    if (data.has_trailers()) {
      ENVOY_LOG_MISC(debug, "Decoding trailers: {} ", data.trailers().DebugString());
      Http::TestHeaderMapImpl trailers = Fuzz::fromHeaders(data.trailers());
      filter->decodeTrailers(trailers);
    }
  }

  // This creates and mutates the filter config and runs decode.
  void fuzz(absl::string_view filter_name, const test::fuzz::HttpData& data) {
    ENVOY_LOG_MISC(info, "Fuzzing filter: {}", filter_name);

    auto& factory =
        Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
            std::string(filter_name));
    auto proto_config = factory.createEmptyConfigProto();

    protobuf_mutator::Mutator mutator;
    try {
      // Mutate and validate the chosen filter protobuf directly.
      mutator.Mutate(proto_config.get(), 200);
      Http::FilterFactoryCb cb =
          factory.createFilterFactoryFromProto(*proto_config, "stats", factory_context_);
      ENVOY_LOG_MISC(debug, "Mutated created configuration: {}", proto_config->DebugString());
      cb(filter_callback_);
    } catch (const EnvoyException& e) {
      ENVOY_LOG_MISC(debug, "Mutator created an invalid configuration: {}", e.what());
      return;
    }

    decode(filter_.get(), data);
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback_;
  std::shared_ptr<Http::StreamDecoderFilter> filter_;
};

DEFINE_PROTO_FUZZER(const test::extensions::filters::http::FilterFuzzTestCase& input) {
  // Choose the HTTP filter with the fuzzed input int.
  const std::vector<absl::string_view> filter_names = Registry::FactoryRegistry<
      Server::Configuration::NamedHttpFilterConfigFactory>::registeredNames();
  absl::string_view filter_name = filter_names[input.filter_index() % filter_names.size()];

  // Fuzz filter.
  static UberFilterFuzzer fuzzer;
  fuzzer.fuzz(filter_name, input.data());
}

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
