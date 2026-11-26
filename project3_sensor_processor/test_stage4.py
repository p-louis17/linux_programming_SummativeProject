import resource
import sensor_processor as sp

# Example from the assignment
print(sp.filter_readings([10.5, 25.0, 110.0, 35.5], 20.0, 40.0))  # expect [25.0, 35.5]

# Edge cases that should work
print(sp.filter_readings((25.0, 35.5, 5.0), 20, 40))      # expect [25.0, 35.5] (list from tuple)
print(sp.filter_readings([], 20, 40))                     # expect []
print(sp.filter_readings([1, 2, 3], 2, 3))                # expect [2.0, 3.0] (ints become floats)
print(sp.filter_readings([20.0, 40.0], 20, 40))           # expect [20.0, 40.0] (inclusive)
print(sp.filter_readings([1.0, 2.0], 50, 60))             # expect [] (nothing matches)
print(sp.filter_readings([float("nan"), 25.0], 20, 40))   # expect [25.0] (NaN dropped)

# filter and count_outliers must always add up to the input length
data = [10.5, 25.0, 110.0, 35.5, float("nan"), 20.0, 40.0]
kept = len(sp.filter_readings(data, 20.0, 40.0))
out = sp.count_outliers(data, 20.0, 40.0)
print(kept, out, kept + out == len(data))                 # expect 4 3 True

# Cases that should raise
tests = [
    ("filter reversed",  lambda: sp.filter_readings([1, 2], 40, 20)),
    ("filter str data",  lambda: sp.filter_readings("abc", 0, 10)),
    ("filter bad bound", lambda: sp.filter_readings([1, 2], None, 10)),
    ("filter bad item",  lambda: sp.filter_readings([1, "x"], 0, 10)),
]
for name, call in tests:
    try:
        call()
        print(name, "-> NO ERROR (unexpected)")
    except (TypeError, ValueError) as e:
        print(name, "->", type(e).__name__ + ":", e)

# Leak check: 20000 calls on 1000 readings. A leak of the result list
# would cost hundreds of MB, so memory should stay nearly flat.
big = [float(i) for i in range(1000)]
before = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
for _ in range(20000):
    sp.filter_readings(big, 100, 900)
after = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
print("peak memory KB before/after:", before, after)