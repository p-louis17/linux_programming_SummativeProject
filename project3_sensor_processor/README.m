# sensor_processor: Python C extension for IoT sensor data

`sensor_processor` is a native C extension for Python that filters and analyses IoT sensor readings. It provides five functions and does all numerical work in C using `double`:

| Function | Purpose |
|---|---|
| `filter_readings(data, minimum, maximum)` | New list of readings inside the inclusive range |
| `count_outliers(data, minimum, maximum)` | Number of readings outside the inclusive range |
| `moving_average(data, window)` | Moving average with the given window size |
| `maximum_difference(data)` | Maximum reading minus minimum reading |
| `sensor_summary(data)` | Dictionary with samples, minimum, maximum, average and above_average |

## Requirements

Developed and tested on Ubuntu Linux with Python 3.12 and GCC. To install the build tools:

```bash
sudo apt install python3-dev python3-setuptools build-essential
```

## Files

| File | Description |
|---|---|
| `sensor_processor.c` | C source: five functions, helpers, method table, module definition, init function |
| `setup.py` | setuptools build configuration |
| `test_sensor_processor.py` | Test program: valid, empty and invalid input (20 checks) |
| `benchmark.py` | Timing comparison of the C module against pure Python |

## Compilation

Run from the project folder:

```bash
python3 setup.py build_ext --inplace
```

Expected result: GCC compiles `sensor_processor.c` with `-O2 -Wall -fPIC` and prints no warnings. A shared library named `sensor_processor.cpython-312-x86_64-linux-gnu.so` appears in the folder. The name depends on your Python version.

Check that it imports:

```bash
python3 -c "import sensor_processor; print(sensor_processor)"
```

Expected output: `<module 'sensor_processor' from '.../sensor_processor.cpython-312-x86_64-linux-gnu.so'>`

For a clean rebuild, run `rm -rf build *.so` first.

## Execution

Run the test program:

```bash
python3 test_sensor_processor.py
```

Run the benchmark (takes a few seconds):

```bash
python3 benchmark.py
```

Or use the module interactively:

```python
>>> import sensor_processor as sp
>>> sp.filter_readings([10.5, 25.0, 110.0, 35.5], 20.0, 40.0)
[25.0, 35.5]
```

The module must be imported from the folder that contains the `.so` file, or from a folder on Python's path.

## Inputs

| Argument | Accepted values |
|---|---|
| `data` | A Python list or tuple of ints and floats. Any other type is rejected. |
| `minimum`, `maximum` | Numbers, with `minimum <= maximum` |
| `window` | An integer from 1 up to the number of readings |

## Functions: expected outputs

| Call | Result |
|---|---|
| `filter_readings([10.5, 25.0, 110.0, 35.5], 20.0, 40.0)` | `[25.0, 35.5]` |
| `count_outliers([10.5, 25.0, 110.0, 35.5], 20.0, 40.0)` | `2` |
| `moving_average([10.0, 20.0, 30.0, 40.0], 2)` | `[15.0, 25.0, 35.0]` |
| `maximum_difference([10.0, 25.0, 40.0, 15.0])` | `30.0` |
| `sensor_summary([10.0, 20.0, 30.0, 40.0])` | `{'samples': 4, 'minimum': 10.0, 'maximum': 40.0, 'average': 25.0, 'above_average': 2}` |

Notes on behaviour:

- Range bounds are inclusive.
- `filter_readings` always returns a list of floats, even for tuple input.
- A reading equal to the mean is not counted in `above_average`.
- NaN is dropped by `filter_readings` and counted as an outlier by `count_outliers`.

## Empty input and errors

| Situation | Behaviour |
|---|---|
| Empty data in `filter_readings` | Returns `[]` |
| Empty data in `count_outliers` | Returns `0` |
| Empty data in `moving_average`, `maximum_difference`, `sensor_summary` | `ValueError: data must not be empty` |
| Data is not a list or tuple | `TypeError: data must be a list or tuple, not str` |
| An element is not a number | `TypeError: must be real number, not NoneType` |
| `minimum` greater than `maximum`, or a NaN bound | `ValueError: minimum must be <= maximum, and neither may be NaN` |
| Window less than 1 | `ValueError: window must be at least 1` |
| Window larger than the data | `ValueError: window (100) must not exceed the number of readings (8)` |
| Window is a float | `TypeError: 'float' object cannot be interpreted as an integer` |
| NaN or infinity in `moving_average`, `maximum_difference`, `sensor_summary` | `ValueError: data contains NaN or infinity` |
| Sum overflows in `sensor_summary` | `OverflowError: sum of readings overflows double` |

## Expected test output

The test program uses these readings: `[22.5, 23.1, 24.8, 110.0, 21.9, 25.3, -5.0, 23.7]`, where 110.0 and -5.0 simulate faulty sensors. With a range of 15 to 35 and a window of 3, the valid checks print:

```
1. Valid input
  PASS  filter_readings(readings, 15, 35): [22.5, 23.1, 24.8, 21.9, 25.3, 23.7]
  PASS  count_outliers(readings, 15, 35): 2
  PASS  moving_average(readings, 3), rounded to 2 places: [23.47, 52.63, 52.23, 52.4, 14.07, 14.67]
  PASS  maximum_difference(readings): 115.0
  PASS  sensor_summary(readings), floats rounded to 4 places: {'samples': 8, 'minimum': -5.0, 'maximum': 110.0, 'average': 30.7875, 'above_average': 1}
  PASS  filter_readings accepts a tuple: [22.5, 23.1, 24.8, 21.9, 25.3, 23.7]
```

It then checks five empty-input cases and nine invalid-input cases, each printing `PASS` with the exception raised. The last line is:

```
20/20 checks passed.
```

The program exits with status 0 when every check passes and a non-zero status otherwise.

## Expected benchmark output

The benchmark times each function on 1,000,000 random readings against an equivalent pure Python version, best of three runs. The `match` column must be `True` for every row. Timings depend on your machine. On the development laptop the C version was faster for every function, by 1.2 to 5.3 times:

```
function              Python ms       C ms   speedup  match
filter_readings            47.0       38.3      1.2x  True
count_outliers             43.7       12.2      3.6x  True
moving_average            143.8       44.8      3.2x  True
maximum_difference         32.8        9.7      3.4x  True
sensor_summary             75.3       14.2      5.3x  True
```

## Design notes

- Each call converts the Python list into one C array of `double` using `PyFloat_AsDouble`, then runs plain C loops. The array is freed on every exit path.
- Result lists and dictionaries are built with correct reference handling, and repeated-call memory tests showed no leaks.
- `moving_average` uses a sliding sum, and `sensor_summary` uses Kahan summation for an accurate mean.
- Every function runs in O(n) time. The full explanation of complexity, memory management and accuracy is in the project report.
- The module must not be compiled with `-ffast-math`, because that can undo the Kahan summation.

## Troubleshooting

| Problem | Fix |
|---|---|
| `fatal error: Python.h: No such file or directory` | Install `python3-dev` |
| `command 'gcc' failed` | Install `build-essential` |
| `ModuleNotFoundError: No module named 'setuptools'` | Install `python3-setuptools` |
| `ModuleNotFoundError: No module named 'sensor_processor'` | Run from the folder containing the `.so` file, or rebuild with the build command |
| Import fails after changing Python version | Rebuild with `rm -rf build *.so` and the build command |
