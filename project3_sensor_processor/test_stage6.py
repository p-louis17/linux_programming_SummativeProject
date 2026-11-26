import math
import random
import resource
import sensor_processor as sp

# Example from the assignment
print(sp.sensor_summary([10.0, 20.0, 30.0, 40.0]))
# expect {'samples': 4, 'minimum': 10.0, 'maximum': 40.0, 'average': 25.0, 'above_average': 2}

# Edge cases that should work
print(sp.sensor_summary([7.5]))          # expect samples 1, min/max/average 7.5, above_average 0
print(sp.sensor_summary((5, 5, 5)))      # expect average 5.0, above_average 0 (equal is not above)
print(sp.sensor_summary([-10, 0, 10, 30]))  # expect average 7.5, above_average 1

# Cross-check against Python on random data
random.seed(2)
data = [random.uniform(-50, 150) for _ in range(10000)]
s = sp.sensor_summary(data)
print(s["samples"] == len(data), s["minimum"] == min(data), s["maximum"] == max(data))  # True True True
print(abs(s["average"] - math.fsum(data) / len(data)))   # expect around 1e-14 or smaller
print(s["above_average"] == sum(1 for v in data if v > s["average"]))  # expect True

# Accuracy: Kahan sum in C vs a plain loop on one million readings
many = [0.1] * 1_000_000
naive = 0.0
for v in many:
    naive += v
print("C (Kahan):   ", sp.sensor_summary(many)["average"])
print("exact fsum:  ", math.fsum(many) / len(many))
print("plain loop:  ", naive / len(many))
# expect the first two to agree closely and the plain loop to be visibly off

# Cases that should raise
tests = [
    ("empty",     lambda: sp.sensor_summary([])),
    ("str data",  lambda: sp.sensor_summary("abc")),
    ("nan",       lambda: sp.sensor_summary([1.0, float("nan")])),
    ("inf",       lambda: sp.sensor_summary([1.0, float("inf")])),
    ("bad item",  lambda: sp.sensor_summary([1, None])),
    ("overflow",  lambda: sp.sensor_summary([1e308, 1e308])),
]
for name, call in tests:
    try:
        call()
        print(name, "-> NO ERROR (unexpected)")
    except (TypeError, ValueError, OverflowError) as e:
        print(name, "->", type(e).__name__ + ":", e)

# Leak check: 20000 calls on 1000 readings
big = [float(i) for i in range(1000)]
before = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
for _ in range(20000):
    sp.sensor_summary(big)
after = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
print("peak memory KB before/after:", before, after)