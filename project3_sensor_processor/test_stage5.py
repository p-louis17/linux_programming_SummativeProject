import random
import resource
import sensor_processor as sp

# Example from the assignment
print(sp.moving_average([10.0, 20.0, 30.0, 40.0], 2))   # expect [15.0, 25.0, 35.0]

# Edge cases that should work
print(sp.moving_average((10, 20, 30), 1))               # expect [10.0, 20.0, 30.0]
print(sp.moving_average([10, 20, 30, 40], 4))           # expect [25.0] (window == length)
print(sp.moving_average([1.0, 2.0, 3.0, 4.0, 5.0], 3))  # expect [2.0, 3.0, 4.0]
print(sp.moving_average([-5.0, 5.0, -5.0, 5.0], 2))     # expect [0.0, 0.0, 0.0]

# Accuracy: compare the sliding sum with a direct recomputation in Python
random.seed(1)
data = [random.uniform(0, 100) for _ in range(10000)]
w = 50
expected = [sum(data[i:i + w]) / w for i in range(len(data) - w + 1)]
got = sp.moving_average(data, w)
print(len(got) == len(expected))                        # expect True
print(max(abs(a - b) for a, b in zip(got, expected)))   # expect far below 1e-9

# Cases that should raise
tests = [
    ("window zero",     lambda: sp.moving_average([1, 2, 3], 0)),
    ("window negative", lambda: sp.moving_average([1, 2, 3], -2)),
    ("window too big",  lambda: sp.moving_average([1, 2, 3], 4)),
    ("window float",    lambda: sp.moving_average([1, 2, 3], 2.0)),
    ("empty data",      lambda: sp.moving_average([], 2)),
    ("str data",        lambda: sp.moving_average("abc", 2)),
    ("nan in data",     lambda: sp.moving_average([1.0, float("nan"), 3.0], 2)),
    ("bad item",        lambda: sp.moving_average([1, None, 3], 2)),
]
for name, call in tests:
    try:
        call()
        print(name, "-> NO ERROR (unexpected)")
    except (TypeError, ValueError) as e:
        print(name, "->", type(e).__name__ + ":", e)

# Leak check: 20000 calls on 1000 readings
big = [float(i) for i in range(1000)]
before = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
for _ in range(20000):
    sp.moving_average(big, 10)
after = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
print("peak memory KB before/after:", before, after)