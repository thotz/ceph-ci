// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include <boost/asio/detail/config.hpp>
#include <boost/asio/detail/scheduler.hpp>
#include <boost/asio/detail/strand_executor_service.hpp>
#include <boost/asio/detail/strand_service.hpp>
#include <boost/asio/detail/thread_info_base.hpp>
#include <boost/asio/detail/thread_context.hpp>
#include <boost/asio/detail/call_stack.hpp>

#ifdef BOOST_ASIO_HAS_THREAD_KEYWORD_EXTENSION
#error "BOOST_ASIO_HAS_THREAD_KEYWORD_EXTENSION is not supported by shared libraries"
#endif

namespace boost {
namespace asio {
namespace detail {

template <typename Key, typename Value>
tss_ptr<typename call_stack<Key, Value>::context>
call_stack<Key, Value>::top_;

} // namespace detail
} // namespace asio
} // namespace boost
