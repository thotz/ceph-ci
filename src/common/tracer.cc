#include "tracer.h"
#include <arpa/inet.h>
#include <yaml-cpp/yaml.h>
#ifdef __linux__
#include <linux/types.h>
#else
typedef int64_t __s64;
#endif

#include "common/debug.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_osd
#undef dout_prefix
#define dout_prefix *_dout << "jaeger-osd "

namespace jaeger_tracing{


  jspan new_span(const char* span_name){
   return opentracing::Tracer::Global()->StartSpan(span_name);
 }

  jspan child_span(const char* span_name, const jspan& parent_span){
  if(parent_span){
    return opentracing::Tracer::Global()->StartSpan(span_name,
	{opentracing::ChildOf(&parent_span->context())}); }
  return nullptr;
 }

  void finish_span(const jspan& span){ if(span){ span->Finish(); } }

  void set_span_tag(const jspan& span, const char* key, const char* value){ if(span)
  span->SetTag(key, value); }
}
