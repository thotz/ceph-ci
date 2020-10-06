#include "tracer.h"
#include <arpa/inet.h>
#include <yaml-cpp/yaml.h>
#ifdef __linux__
#include <linux/types.h>
#else
typedef int64_t __s64;
#endif

#include "common/debug.h"

namespace jaeger_tracing{

   std::shared_ptr<opentracing::v3::Tracer> tracer = nullptr;

   static void init_tracer(const char* tracer_name){
    dout(3) << "init_tracer" << dendl;
    if(!tracer){
	dout(3) << "tracer not found, cofiguring jaegertracing" << dendl;
	YAML::Node yaml;
	try{
	  yaml = YAML::LoadFile("../src/jaegertracing/config.yml");
	}
	catch(std::exception &e){
	  dout(3) << "failed to load yaml file using default config" << dendl;
	  auto yaml_config = R"cfg(
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
	yaml = YAML::Load(yaml_config);
      }
      static auto configuration = jaegertracing::Config::parse(yaml);
      dout(3) << "yaml parsed" << yaml << dendl;
      tracer = jaegertracing::Tracer::make( tracer_name, configuration,
	    jaegertracing::logging::consoleLogger());
      dout(3) << "tracer_jaeger" << tracer << dendl;
    }
    //incase of stale tracer, configure with a new global tracer
    if(opentracing::Tracer::Global() != tracer){
      opentracing::Tracer::InitGlobal(
	  std::static_pointer_cast<opentracing::Tracer>(tracer));
      dout(3) << "tracer_global_set_to" << tracer << dendl;
    }
  //test spans
  auto parent_span = tracer->StartSpan("parent");
  assert(parent_span);
  parent_span->Finish();
  }

  jspan new_span(const char* span_name){
    dout(3) << "new_span before" + std::string(span_name) << dendl;
    return opentracing::Tracer::Global()->StartSpan(span_name);
    dout(3) << "new_span after" + std::string(span_name) << dendl;
 }

  jspan child_span(const char* span_name, const jspan& parent_span){
    //no parent check if parent not found span will still be constructed
    return opentracing::Tracer::Global()->StartSpan(span_name,
	{opentracing::ChildOf(&parent_span->context())});
    dout(3) << "child_span after" + std::string(span_name) << dendl;
 }

  void finish_span(const jspan& span){
    if(span){ span->Finish(); }
  }

  void set_span_tag(const jspan& span, const char* key, const char* value){ if(span)
  span->SetTag(key, value); }
}
