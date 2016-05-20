/* Copyright (c) 2016, the Cap authors.
 *
 * This file is subject to the Modified BSD License and may not be distributed
 * without copyright and license information. Please refer to the file LICENSE
 * for the text and further information on this license.
 */

#define BOOST_TEST_MODULE Timer

#include "main.cc"

#include <cap/timer.h>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <fstream>
#include <thread>

namespace cap
{
BOOST_AUTO_TEST_CASE(test_timer)
{
  unsigned int const tolerance = 15;
  Timer timer(boost::mpi::communicator(), "test");

  timer.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  timer.stop();
  boost::chrono::process_cpu_clock::duration duration =
      timer.get_elapsed_time();
  boost::chrono::milliseconds ms =
      boost::chrono::duration_cast<boost::chrono::milliseconds>(duration);
  BOOST_TEST(std::abs(ms.count() - 200) < tolerance);

  timer.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  timer.stop();
  duration = timer.get_elapsed_time();
  ms = boost::chrono::duration_cast<boost::chrono::milliseconds>(duration);
  BOOST_TEST(std::abs(ms.count() - 400) < 2 * tolerance);

  timer.reset();
  timer.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  timer.stop();
  duration = timer.get_elapsed_time();
  ms = boost::chrono::duration_cast<boost::chrono::milliseconds>(duration);
  BOOST_TEST(std::abs(ms.count() - 200) < tolerance);
}
}
