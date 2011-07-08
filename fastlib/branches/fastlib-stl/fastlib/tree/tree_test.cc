/**
 * @file uselapack_test.cc
 *
 * Tests for LAPACK integration.
 */

#include "bounds.h"

#define BOOST_TEST_MODULE Something
#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_CASE(TestBallBound) {
  DBallBound<> b1;
  DBallBound<> b2;

  // Create two balls with a center distance of 1 from each other.
  // Give the first one a radius of 0.3 and the second a radius of 0.4.

  b1.center().set_size(3);
  b1.center()[0] = 1;
  b1.center()[1] = 2;
  b1.center()[2] = 3;
  b1.set_radius(0.3);

  b2.center().set_size(3);
  b2.center()[0] = 1;
  b2.center()[1] = 2;
  b2.center()[2] = 4;
  b2.set_radius(0.4);

  BOOST_REQUIRE_CLOSE(sqrt(b1.MinDistanceSq(b2)), 1-0.3-0.4, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b1.RangeDistanceSq(b2).hi), 1+0.3+0.4, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b1.RangeDistanceSq(b2).lo), 1-0.3-0.4, 1e-5);
  BOOST_REQUIRE_CLOSE(b1.RangeDistance(b2).hi, 1+0.3+0.4, 1e-5);
  BOOST_REQUIRE_CLOSE(b1.RangeDistance(b2).lo, 1-0.3-0.4, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b1.MinToMidSq(b2)), 1-0.3, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b1.MinimaxDistanceSq(b2)), 1-0.3+0.4, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b1.MidDistanceSq(b2)), 1, 1e-5);

  BOOST_REQUIRE_CLOSE(sqrt(b2.MinDistanceSq(b1)), 1-0.3-0.4, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b2.MaxDistanceSq(b1)), 1+0.3+0.4, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b2.RangeDistanceSq(b1).hi), 1+0.3+0.4, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b2.RangeDistanceSq(b1).lo), 1-0.3-0.4, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b2.MinToMidSq(b1)), 1-0.4, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b2.MinimaxDistanceSq(b1)), 1-0.4+0.3, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b2.MidDistanceSq(b1)), 1, 1e-5);

  BOOST_REQUIRE(b1.Contains(b1.center()));
  BOOST_REQUIRE(!b1.Contains(b2.center()));

  BOOST_REQUIRE(!b2.Contains(b1.center()));
  BOOST_REQUIRE(b2.Contains(b2.center()));
  arma::vec b2point(3); // a point that's within the radius bot not the center
  b2point[0] = 1.1;
  b2point[1] = 2.1;
  b2point[2] = 4.1;

  BOOST_REQUIRE(b2.Contains(b2point));

  BOOST_REQUIRE_CLOSE(sqrt(b1.MinDistanceSq(b1.center())), 0, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b1.MinDistanceSq(b2.center())), 1-0.3, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b2.MinDistanceSq(b1.center())), 1-0.4, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b2.MaxDistanceSq(b1.center())), 1+0.4, 1e-5);
  BOOST_REQUIRE_CLOSE(sqrt(b1.MaxDistanceSq(b2.center())), 1+0.3, 1e-5);
}
