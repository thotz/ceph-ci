// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "test/crimson/gtest_seastar.h"

#include "crimson/common/interruptible_future.h"

class test_interruption : public std::exception
{};

class TestInterruptCondition {
public:
  TestInterruptCondition(bool interrupt)
    : interrupt(interrupt) {}

  template <typename T>
  std::pair<bool, std::optional<T>> may_interrupt() {
    bool interrupt = (std::rand() % 2) == 0;
    if (interrupt)
      return std::pair<bool, std::optional<T>>(
	  true, seastar::futurize<T>::make_exception_future(test_interruption()));
    else
      return std::pair<bool, std::optional<T>>(false, std::optional<T>());
  }

  template <typename T>
  static constexpr bool is_interruption_v = std::is_same_v<T, test_interruption>;

  bool is_interruption(std::exception_ptr& eptr) {
    if (*eptr.__cxa_exception_type() == typeid(test_interruption))
      return true;
    return false;
  }
private:
  bool interrupt = false;
};

TEST_F(seastar_test_suite_t, basic)
{
  using interruptor =
    ::crimson::interruptible::interruptor<TestInterruptCondition>;
  run_async([this] {
    interruptor::with_interruption(
      [] {
	ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	return interruptor::make_interruptible(seastar::now())
	.then_interruptible([] {
	  ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	}).then_interruptible([] {
	  ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	  return ::crimson::errorator<::crimson::ct_error::enoent>::make_ready_future<>();
	}).safe_then_interruptible([] {
	  ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	  return seastar::now();
	}, ::crimson::errorator<::crimson::ct_error::enoent>::all_same_way([] {
	  ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	  })
	);
      }, [](std::exception_ptr) {}, false).get0();

    interruptor::with_interruption(
      [] {
	ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	return interruptor::make_interruptible(seastar::now())
	.then_interruptible([] {
	  ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	});
      }, [](std::exception_ptr) {
	ceph_assert(!::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	return seastar::now();
      }, true).get0();


  });
}

TEST_F(seastar_test_suite_t, loops)
{
  using interruptor =
    ::crimson::interruptible::interruptor<TestInterruptCondition>;
  run_async([this] {
    interruptor::with_interruption(
      [] {
	ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	return interruptor::make_interruptible(seastar::now())
	.then_interruptible([] {
	  std::vector<int> vec = {1, 2};
	  return seastar::do_with(std::move(vec), [](auto& vec) {
	    return interruptor::do_for_each(std::begin(vec), std::end(vec), [](int) {
	      ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	      return seastar::now();
	    });
	  });
	}).then_interruptible([] {
	  std::vector<int> vec = {1, 2};
	  return seastar::do_with(std::move(vec), [](auto& vec) {
	    return interruptor::do_for_each(std::begin(vec), std::end(vec), [](int) {
	      ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	      return interruptor::make_interruptible(seastar::now());
	    });
	  });
	}).then_interruptible([] {
	  return interruptor::repeat([] {
	    ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	    return interruptor::make_interruptible(
		seastar::make_ready_future<
		  seastar::stop_iteration>(
		    seastar::stop_iteration::yes));
	  });
	}).then_interruptible([] {
	  return interruptor::repeat([] {
	    ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	    return seastar::make_ready_future<
		    seastar::stop_iteration>(
		      seastar::stop_iteration::yes);
	  });
	}).then_interruptible([] {
	  std::vector<int> vec = {1, 2};
	  return seastar::do_with(std::move(vec), [](auto& vec) {
	    return interruptor::do_for_each(std::begin(vec), std::end(vec), [](int) {
	      ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	      return interruptor::make_interruptible(
		::crimson::errorator<::crimson::ct_error::enoent>::make_ready_future<>());
	    });
	  });
	}).safe_then_interruptible([] {
	  ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	  return seastar::now();
	}, ::crimson::errorator<::crimson::ct_error::enoent>::all_same_way([] {
	  ceph_assert(::crimson::interruptible::interrupt_cond<TestInterruptCondition>);
	  })
	);;
      }, [](std::exception_ptr) {}, false).get0();
  });
}
