
add_executable(ceph_test_neorados test_neorados.cc)
target_link_libraries(ceph_test_neorados
  global
  libneorados
  ${unittest_libs}
  radostest
  radostest-cxx
  librados
  $<TARGET_OBJECTS:common_asio_static_variables_obj>
  GTest::GTest)

add_executable(ceph_test_neorados_start_stop start_stop.cc)
target_link_libraries(ceph_test_neorados_start_stop
  global
  libneorados
  $<TARGET_OBJECTS:common_asio_static_variables_obj>
  ${unittest_libs})

add_executable(ceph_test_neorados_completions completions.cc)
target_link_libraries(ceph_test_neorados_completions
  $<TARGET_OBJECTS:common_asio_static_variables_obj>
  Boost::system
  pthread
  ${unittest_libs})

add_executable(ceph_test_neorados_op_speed op_speed.cc)
target_link_libraries(ceph_test_neorados_op_speed
  libneorados
  $<TARGET_OBJECTS:common_asio_static_variables_obj>
  fmt::fmt
  ${unittest_libs})

add_library(neoradostest-support common_tests.cc)
target_link_libraries(neoradostest-support
  libneorados
  $<TARGET_OBJECTS:common_asio_static_variables_obj>
  fmt::fmt)

add_executable(ceph_test_neorados_list_pool list_pool.cc)
target_link_libraries(ceph_test_neorados_list_pool
  libneorados
  neoradostest-support
  global
  $<TARGET_OBJECTS:common_asio_static_variables_obj>
  fmt::fmt
  ${unittest_libs})
