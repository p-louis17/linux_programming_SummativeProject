"""Compare the C extension with equivalent pure Python on 1,000,000 readings."""
import random
import time

import sensor_processor as sp

N = 1_000_000
LO, HI, W = 20.0, 80.0, 50

random.seed(3)
data = [random.uniform(0, 100) for _ in range(N)]


# Pure Python versions, written the way a Python application would do it.
def py_filter(d):
    return [v for v in d if LO <= v <= HI]


def py_outliers(d):
    return sum(1 for v in d if not (LO <= v <= HI))


def py_moving(d):
    # Same sliding sum algorithm as the C version, for a fair comparison.
    s = sum(d[:W])
    out = [s / W]
    for i in range(W, len(d)):
        s += d[i] - d[i - W]
        out.append(s / W)
    return out


def py_diff(d):
    return max(d) - min(d)


def py_summary(d):
    avg = sum(d) / len(d)
    return {"samples": len(d), "minimum": min(d), "maximum": max(d),
            "average": avg, "above_average": sum(1 for v in d if v > avg)}


# Output checks, so we never time a wrong answer.
def same_moving(a, b):
    return len(a) == len(b) and max(abs(x - y) for x, y in zip(a, b)) < 1e-9


def same_summary(a, b):
    return (a["samples"] == b["samples"] and a["minimum"] == b["minimum"]
            and a["maximum"] == b["maximum"]
            and abs(a["average"] - b["average"]) < 1e-9
            and a["above_average"] == b["above_average"])


def best_time(func, repeats=3):
    best = float("inf")
    for _ in range(repeats):
        start = time.perf_counter()
        func()
        best = min(best, time.perf_counter() - start)
    return best


cases = [
    ("filter_readings",
     lambda: py_filter(data), lambda: sp.filter_readings(data, LO, HI),
     lambda a, b: a == b),
    ("count_outliers",
     lambda: py_outliers(data), lambda: sp.count_outliers(data, LO, HI),
     lambda a, b: a == b),
    ("moving_average",
     lambda: py_moving(data), lambda: sp.moving_average(data, W),
     same_moving),
    ("maximum_difference",
     lambda: py_diff(data), lambda: sp.maximum_difference(data),
     lambda a, b: a == b),
    ("sensor_summary",
     lambda: py_summary(data), lambda: sp.sensor_summary(data),
     same_summary),
]

print(f"{N:,} readings, best of 3 runs\n")
print(f"{'function':20s} {'Python ms':>10s} {'C ms':>10s} {'speedup':>9s}  match")
for name, py, c, same in cases:
    match = same(py(), c())
    t_py = best_time(py)
    t_c = best_time(c)
    print(f"{name:20s} {t_py * 1000:10.1f} {t_c * 1000:10.1f} {t_py / t_c:8.1f}x  {match}")