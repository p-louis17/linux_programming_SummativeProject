"""Test program for the sensor_processor C extension (Project 3).

Calls all five functions on sample readings, then checks empty input
and several invalid inputs. Each check prints PASS or FAIL.
"""
import math
import sys

import sensor_processor as sp

passed = 0
total = 0


def close(a, b):
    """Compare results, allowing tiny floating point differences."""
    if isinstance(a, dict):
        return a.keys() == b.keys() and all(close(a[k], b[k]) for k in a)
    if isinstance(a, list):
        return len(a) == len(b) and all(close(x, y) for x, y in zip(a, b))
    if isinstance(a, float):
        return math.isclose(a, b, rel_tol=1e-9, abs_tol=1e-9)
    return a == b


def check(label, got, expected):
    """Check that a function returned the expected value."""
    global passed, total
    total += 1
    ok = close(got, expected)
    passed += ok
    print(f"  {'PASS' if ok else 'FAIL'}  {label}: {got}")
    if not ok:
        print(f"        expected: {expected}")


def check_error(label, exc_type, func, *args):
    """Check that a call raises the expected exception type."""
    global passed, total
    total += 1
    try:
        func(*args)
    except exc_type as e:
        passed += 1
        print(f"  PASS  {label}: {type(e).__name__}: {e}")
    except Exception as e:
        print(f"  FAIL  {label}: wrong exception {type(e).__name__}: {e}")
    else:
        print(f"  FAIL  {label}: no exception raised")


# Sample temperature readings in degrees C. 110.0 and -5.0 are faults.
readings = [22.5, 23.1, 24.8, 110.0, 21.9, 25.3, -5.0, 23.7]
print("Sample readings:", readings)

print("\n1. Valid input")
check("filter_readings(readings, 15, 35)",
      sp.filter_readings(readings, 15.0, 35.0),
      [22.5, 23.1, 24.8, 21.9, 25.3, 23.7])
check("count_outliers(readings, 15, 35)",
      sp.count_outliers(readings, 15.0, 35.0), 2)
check("moving_average(readings, 3), rounded to 2 places",
      [round(v, 2) for v in sp.moving_average(readings, 3)],
      [23.47, 52.63, 52.23, 52.4, 14.07, 14.67])
check("maximum_difference(readings)",
      sp.maximum_difference(readings), 115.0)
summary = sp.sensor_summary(readings)
check("sensor_summary(readings), floats rounded to 4 places",
      {k: (round(v, 4) if isinstance(v, float) else v) for k, v in summary.items()},
      {"samples": 8, "minimum": -5.0, "maximum": 110.0,
       "average": 30.7875, "above_average": 1})
check("filter_readings accepts a tuple",
      sp.filter_readings(tuple(readings), 15.0, 35.0),
      [22.5, 23.1, 24.8, 21.9, 25.3, 23.7])

print("\n2. Empty input")
check("filter_readings([]) returns an empty list",
      sp.filter_readings([], 15.0, 35.0), [])
check("count_outliers([]) returns 0",
      sp.count_outliers([], 15.0, 35.0), 0)
check_error("moving_average([], 3)", ValueError, sp.moving_average, [], 3)
check_error("maximum_difference([])", ValueError, sp.maximum_difference, [])
check_error("sensor_summary([])", ValueError, sp.sensor_summary, [])

print("\n3. Invalid input")
check_error("filter_readings with a string", TypeError,
            sp.filter_readings, "abc", 15.0, 35.0)
check_error("count_outliers with an int instead of a list", TypeError,
            sp.count_outliers, 5, 15.0, 35.0)
check_error("filter_readings with minimum > maximum", ValueError,
            sp.filter_readings, readings, 35.0, 15.0)
check_error("count_outliers with a non-numeric bound", TypeError,
            sp.count_outliers, readings, "low", 35.0)
check_error("moving_average with window 0", ValueError,
            sp.moving_average, readings, 0)
check_error("moving_average with window longer than data", ValueError,
            sp.moving_average, readings, 100)
check_error("moving_average with a float window", TypeError,
            sp.moving_average, readings, 2.5)
check_error("sensor_summary with a None item", TypeError,
            sp.sensor_summary, [1.0, None, 3.0])
check_error("maximum_difference with NaN", ValueError,
            sp.maximum_difference, [1.0, float("nan")])

print(f"\n{passed}/{total} checks passed.")
sys.exit(0 if passed == total else 1)