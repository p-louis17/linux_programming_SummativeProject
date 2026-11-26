#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <math.h>

/* ------------------------------------------------------------------
 * load_readings
 *
 * Converts a Python list or tuple of numbers into a newly allocated
 * C array of double.
 *
 * On success: returns 0, *out points to the array (caller must free it
 * with PyMem_Free), *n_out holds the element count (may be 0).
 * On failure: returns -1 with a Python exception set, and nothing
 * is left allocated.
 *
 * Time complexity: O(n). Space: O(n) for the array.
 * ------------------------------------------------------------------ */
static int load_readings(PyObject *obj, double **out, Py_ssize_t *n_out)
{
    /* Reject every type except list and tuple. Strings, dicts, numbers
       and generators are all refused here. */
    if (!PyList_Check(obj) && !PyTuple_Check(obj)) {
        PyErr_Format(PyExc_TypeError,
                     "data must be a list or tuple, not %.100s",
                     Py_TYPE(obj)->tp_name);
        return -1;
    }

    /* Works on both lists and tuples. Size is read once and reused. */
    Py_ssize_t n = PySequence_Fast_GET_SIZE(obj);

    /* One allocation for the whole array. PyMem_Malloc(0) still returns
       a valid pointer, so an empty input needs no special case here. */
    double *values = (double *)PyMem_Malloc((size_t)n * sizeof(double));
    if (values == NULL) {
        PyErr_NoMemory();
        return -1;
    }

    for (Py_ssize_t i = 0; i < n; i++) {
        /* Borrowed reference: we do not own it. */
        PyObject *item = PySequence_Fast_GET_ITEM(obj, i);

        /* Take our own reference while converting, in case __float__
           runs Python code that changes the container. */
        Py_INCREF(item);
        double value = PyFloat_AsDouble(item);
        Py_DECREF(item);

        /* -1.0 is also a legal reading, so the return value alone
           cannot signal failure. PyErr_Occurred() settles it. */
        if (value == -1.0 && PyErr_Occurred()) {
            PyMem_Free(values);
            return -1;
        }
        values[i] = value;
    }

    *out = values;
    *n_out = n;
    return 0;
}

/* ------------------------------------------------------------------
 * all_finite
 *
 * Returns 1 if every value is a normal finite number, 0 if any value
 * is NaN or infinity. Used where such values would make the result
 * meaningless (for example inf - inf is NaN).
 * ------------------------------------------------------------------ */
static int all_finite(const double *values, Py_ssize_t n)
{
    for (Py_ssize_t i = 0; i < n; i++) {
        if (!isfinite(values[i])) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------
 * check_range
 *
 * Validates the [minimum, maximum] arguments. Written as
 * !(minimum <= maximum) so that a NaN bound is rejected too, because
 * every comparison involving NaN is false.
 * ------------------------------------------------------------------ */
static int check_range(double minimum, double maximum)
{
    if (!(minimum <= maximum)) {
        PyErr_SetString(PyExc_ValueError,
                        "minimum must be <= maximum, and neither may be NaN");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------
 * filter_readings(data, minimum, maximum) -> list of float
 *
 * Keeps readings v with minimum <= v <= maximum (bounds inclusive).
 * NaN fails both comparisons, so NaN readings are dropped.
 *
 * Two passes over the C array:
 *   pass 1 counts the matches, so the list can be created at its final
 *          size (no repeated resizing as with append);
 *   pass 2 fills the list.
 *
 * Reference counting:
 *   PyList_New and PyFloat_FromDouble return NEW references (we own them).
 *   PyList_SET_ITEM STEALS a reference: after it, the list owns the
 *   float, so we must not Py_DECREF it ourselves.
 *   On failure, Py_DECREF(result) frees the list and every float already
 *   placed in it. Unfilled slots are NULL, which the list handles safely.
 *
 * Time complexity: O(n) (conversion + two scans). Space: O(n) for the
 * array plus O(k) for the k kept readings.
 * ------------------------------------------------------------------ */
static PyObject *filter_readings(PyObject *self, PyObject *args)
{
    (void)self;
    PyObject *data;
    double minimum, maximum;

    if (!PyArg_ParseTuple(args, "Odd", &data, &minimum, &maximum)) {
        return NULL;
    }
    if (check_range(minimum, maximum) < 0) {
        return NULL;
    }

    double *values = NULL;
    Py_ssize_t n = 0;
    if (load_readings(data, &values, &n) < 0) {
        return NULL;
    }

    /* Pass 1: count readings inside the range. */
    Py_ssize_t keep = 0;
    for (Py_ssize_t i = 0; i < n; i++) {
        if (values[i] >= minimum && values[i] <= maximum) {
            keep++;
        }
    }

    PyObject *result = PyList_New(keep);   /* new reference */
    if (result == NULL) {
        PyMem_Free(values);
        return NULL;
    }

    /* Pass 2: fill the list. */
    Py_ssize_t j = 0;
    for (Py_ssize_t i = 0; i < n; i++) {
        if (values[i] >= minimum && values[i] <= maximum) {
            PyObject *number = PyFloat_FromDouble(values[i]);   /* new ref */
            if (number == NULL) {
                Py_DECREF(result);
                PyMem_Free(values);
                return NULL;
            }
            PyList_SET_ITEM(result, j, number);   /* list now owns number */
            j++;
        }
    }

    PyMem_Free(values);
    return result;   /* ownership passes to the caller */
}

/* ------------------------------------------------------------------
 * moving_average(data, window) -> list of float
 *
 * Formula for output k (k = 0 .. n - window):
 *     avg[k] = (values[k] + values[k+1] + ... + values[k+window-1]) / window
 * Output length is n - window + 1.
 *
 * Sliding sum: computing each window sum from scratch costs
 * O(n * window). Moving the window one step changes only two readings:
 *     sum_new = sum_old + values[k+window-1] - values[k-1]
 * so the whole function is O(n) regardless of the window size.
 *
 * Numerical accuracy: a running sum carries a tiny rounding error from
 * step to step, so results can drift slightly from an exact recomputation
 * (negligible for sensor data, measured in the test script). NaN and
 * infinity are rejected up front, because inf - inf = NaN would poison
 * every later output.
 *
 * Validation: window must be an integer >= 1 ("n" format rejects floats),
 * data must not be empty, and window must not exceed the data length.
 *
 * Time complexity: O(n). Space: O(n) array plus O(n - window + 1) result.
 * ------------------------------------------------------------------ */
static PyObject *moving_average(PyObject *self, PyObject *args)
{
    (void)self;
    PyObject *data;
    Py_ssize_t window;

    /* "n" converts a Python int to Py_ssize_t; floats raise TypeError. */
    if (!PyArg_ParseTuple(args, "On", &data, &window)) {
        return NULL;
    }

    /* Cheap validation first, before any allocation. */
    if (window < 1) {
        PyErr_SetString(PyExc_ValueError, "window must be at least 1");
        return NULL;
    }

    double *values = NULL;
    Py_ssize_t n = 0;
    if (load_readings(data, &values, &n) < 0) {
        return NULL;
    }

    if (n == 0) {
        PyMem_Free(values);
        PyErr_SetString(PyExc_ValueError, "data must not be empty");
        return NULL;
    }
    if (window > n) {
        PyMem_Free(values);
        PyErr_Format(PyExc_ValueError,
                     "window (%zd) must not exceed the number of readings (%zd)",
                     window, n);
        return NULL;
    }
    if (!all_finite(values, n)) {
        PyMem_Free(values);
        PyErr_SetString(PyExc_ValueError, "data contains NaN or infinity");
        return NULL;
    }

    Py_ssize_t count = n - window + 1;
    PyObject *result = PyList_New(count);   /* new reference */
    if (result == NULL) {
        PyMem_Free(values);
        return NULL;
    }

    /* Sum of the first window. */
    double sum = 0.0;
    for (Py_ssize_t i = 0; i < window; i++) {
        sum += values[i];
    }

    for (Py_ssize_t k = 0; k < count; k++) {
        if (k > 0) {
            /* Slide: add the reading entering, subtract the one leaving. */
            sum += values[k + window - 1] - values[k - 1];
        }
        PyObject *average = PyFloat_FromDouble(sum / (double)window);
        if (average == NULL) {
            Py_DECREF(result);
            PyMem_Free(values);
            return NULL;
        }
        PyList_SET_ITEM(result, k, average);   /* list now owns average */
    }

    PyMem_Free(values);
    return result;
}

/* ------------------------------------------------------------------
 * maximum_difference(data) -> float
 *
 * Formula: max(data) - min(data)
 * One pass keeps a running minimum and maximum.
 * Time complexity: O(n) for conversion plus O(n) for the scan, so O(n).
 * Empty data raises ValueError (no maximum exists).
 * ------------------------------------------------------------------ */
static PyObject *maximum_difference(PyObject *self, PyObject *args)
{
    (void)self;
    PyObject *data;

    /* "O" gives us the argument as a borrowed PyObject pointer. */
    if (!PyArg_ParseTuple(args, "O", &data)) {
        return NULL;
    }

    double *values = NULL;
    Py_ssize_t n = 0;
    if (load_readings(data, &values, &n) < 0) {
        return NULL;
    }

    if (n == 0) {
        PyMem_Free(values);
        PyErr_SetString(PyExc_ValueError, "data must not be empty");
        return NULL;
    }
    if (!all_finite(values, n)) {
        PyMem_Free(values);
        PyErr_SetString(PyExc_ValueError, "data contains NaN or infinity");
        return NULL;
    }

    double lowest = values[0];
    double highest = values[0];
    for (Py_ssize_t i = 1; i < n; i++) {
        if (values[i] < lowest)  lowest = values[i];
        if (values[i] > highest) highest = values[i];
    }
    double difference = highest - lowest;

    PyMem_Free(values);

    /* Builds a new Python float; we return the new reference. */
    return PyFloat_FromDouble(difference);
}

/* ------------------------------------------------------------------
 * count_outliers(data, minimum, maximum) -> int
 *
 * A reading is an outlier when it is NOT inside [minimum, maximum]
 * (bounds inclusive). The test is !(v >= minimum && v <= maximum), so a
 * NaN reading counts as an outlier. The more obvious
 * (v < minimum || v > maximum) would wrongly treat NaN as valid.
 * Time complexity: O(n). Empty data returns 0.
 * ------------------------------------------------------------------ */
static PyObject *count_outliers(PyObject *self, PyObject *args)
{
    (void)self;
    PyObject *data;
    double minimum, maximum;

    /* "d" converts ints and floats to double, and raises TypeError
       for anything else, so the range arguments are type-checked too. */
    if (!PyArg_ParseTuple(args, "Odd", &data, &minimum, &maximum)) {
        return NULL;
    }

    /* Cheap validation first, before any allocation. */
    if (check_range(minimum, maximum) < 0) {
        return NULL;
    }

    double *values = NULL;
    Py_ssize_t n = 0;
    if (load_readings(data, &values, &n) < 0) {
        return NULL;
    }

    Py_ssize_t count = 0;
    for (Py_ssize_t i = 0; i < n; i++) {
        if (!(values[i] >= minimum && values[i] <= maximum)) {
            count++;
        }
    }

    PyMem_Free(values);
    return PyLong_FromSsize_t(count);
}

/* ------------------------------------------------------------------
 * sensor_summary(data) -> dict
 *
 * Returns {"samples", "minimum", "maximum", "average", "above_average"}.
 *
 * Formulas:
 *     average       = (x_0 + x_1 + ... + x_{n-1}) / n
 *     above_average = number of i with x_i > average   (strictly greater)
 *
 * Pass 1 finds the minimum, maximum and the sum together.
 * Pass 2 counts readings above the mean; it needs the finished mean, so
 * it cannot be merged into pass 1.
 *
 * Numerical accuracy: the sum uses Kahan compensated summation. A plain
 * loop loses a little precision on every addition and the loss grows with
 * n. Kahan keeps the lost low-order bits in `comp` and feeds them into the
 * next addition, at the cost of a few extra operations per element. It
 * relies on strict floating-point semantics, so the extension must not be
 * compiled with -ffast-math.
 *
 * Errors: empty data and NaN/infinity raise ValueError; a sum that
 * overflows double raises OverflowError.
 *
 * Reference handling: Py_BuildValue creates the dict and all value objects,
 * inserts them, and cleans up if anything fails. (Building by hand with
 * PyDict_SetItemString would need a Py_DECREF of each value afterwards,
 * because that function does NOT steal references.)
 *
 * Time complexity: O(n) conversion + O(n) pass 1 + O(n) pass 2 = O(n).
 * Space: O(n) for the array.
 * ------------------------------------------------------------------ */
static PyObject *sensor_summary(PyObject *self, PyObject *args)
{
    (void)self;
    PyObject *data;

    if (!PyArg_ParseTuple(args, "O", &data)) {
        return NULL;
    }

    double *values = NULL;
    Py_ssize_t n = 0;
    if (load_readings(data, &values, &n) < 0) {
        return NULL;
    }

    if (n == 0) {
        PyMem_Free(values);
        PyErr_SetString(PyExc_ValueError, "data must not be empty");
        return NULL;
    }
    if (!all_finite(values, n)) {
        PyMem_Free(values);
        PyErr_SetString(PyExc_ValueError, "data contains NaN or infinity");
        return NULL;
    }

    /* Pass 1: minimum, maximum, and Kahan sum. */
    double lowest = values[0];
    double highest = values[0];
    double sum = 0.0;
    double comp = 0.0;   /* running compensation for lost low-order bits */
    for (Py_ssize_t i = 0; i < n; i++) {
        if (values[i] < lowest)  lowest = values[i];
        if (values[i] > highest) highest = values[i];

        double y = values[i] - comp;
        double t = sum + y;
        comp = (t - sum) - y;
        sum = t;
    }

    if (!isfinite(sum)) {
        PyMem_Free(values);
        PyErr_SetString(PyExc_OverflowError, "sum of readings overflows double");
        return NULL;
    }
    double mean = sum / (double)n;

    /* Pass 2: count readings strictly above the mean. */
    Py_ssize_t above = 0;
    for (Py_ssize_t i = 0; i < n; i++) {
        if (values[i] > mean) {
            above++;
        }
    }

    PyMem_Free(values);

    /* s = key (C string), n = Py_ssize_t, d = double. */
    return Py_BuildValue("{s:n,s:d,s:d,s:d,s:n}",
                         "samples", n,
                         "minimum", lowest,
                         "maximum", highest,
                         "average", mean,
                         "above_average", above);
}

/* Method table: functions Python can call. */
static PyMethodDef SensorMethods[] = {
    {"filter_readings", filter_readings, METH_VARARGS,
     "filter_readings(data, minimum, maximum) -> list\n"
     "Return a new list of readings inside the inclusive range."},
    {"count_outliers", count_outliers, METH_VARARGS,
     "count_outliers(data, minimum, maximum) -> int\n"
     "Count readings outside the inclusive range [minimum, maximum]."},
    {"moving_average", moving_average, METH_VARARGS,
     "moving_average(data, window) -> list\n"
     "Return the moving average using the given integer window size."},
    {"maximum_difference", maximum_difference, METH_VARARGS,
     "maximum_difference(data) -> float\n"
     "Return max(data) - min(data). Raises ValueError on empty data."},
    {"sensor_summary", sensor_summary, METH_VARARGS,
     "sensor_summary(data) -> dict\n"
     "Return samples, minimum, maximum, average and above_average."},
    {NULL, NULL, 0, NULL}
};

/* Module definition. */
static struct PyModuleDef sensor_module = {
    PyModuleDef_HEAD_INIT,
    "sensor_processor",
    "C extension for IoT sensor data filtering and analysis.",
    -1,
    SensorMethods
};

/* Init function: Python calls this when the module is imported. */
PyMODINIT_FUNC PyInit_sensor_processor(void)
{
    return PyModule_Create(&sensor_module);
}