#include "tracer.h"

 std::shared_ptr<opentracing::v3::Tracer> jaeger_tracing::tracer = nullptr;

 inline void jaeger_tracing::init_tracer(const char* tracerName){
  try{
    auto yaml = R"cfg(
  disabled: false
  reporter:
      logSpans: false
      queueSize: 100
      bufferFlushInterval: 10
  sampler:
    type: const
    param: 1
  headers:
      jaegerDebugHeader: debug-id
      jaegerBaggageHeader: baggage
      TraceContextHeaderName: trace-id
      traceBaggageHeaderPrefix: "testctx-"
  baggage_restrictions:
      denyBaggageOnInitializationFailure: false
      refreshInterval: 60
  )cfg";
  const auto configuration = jaegertracing::Config::parse(YAML::Load(yaml));
  auto tracer = jaegertracing::Tracer::make( tracerName, configuration,
	jaegertracing::logging::consoleLogger());
  }catch(...) { return; }

  opentracing::Tracer::InitGlobal(
      std::_pointer_cast<opentracing::Tracer>(tracer)); }

 inline jspan jaeger_tracing::new_span(const char* span_name){ 
   return opentracing::Tracer::Global()->StartSpan(span_name); 
 }

 inline jspan jaeger_tracing::child_span(const char* span_name, const jspan* const parent_span){
  if(parent_span){ 
    return opentracing::Tracer::Global()->StartSpan(span_name,
	{opentracing::ChildOf(&parent_span->context())}); } return nullptr;
 }

 inline void jaeger_tracing::finish_span(jspan* span){ if(span){ span->Finish(); } }

 inline void jaeger_tracing::set_span_tag(jspan* span, const char* key, const char* value){ if(span)
  span->SetTag(key, value); }
