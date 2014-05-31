#ifndef _CAIRO_COMMON_H_
#define _CAIRO_COMMON_H_

//#include "cairo-boilerplate.h"

#include <stdarg.h>
#include <stdint.h>
#include <math.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ARRAY_LENGTH(__array) ((int) (sizeof (__array) / sizeof (__array[0])))

#define CAIRO_TEST_OUTPUT_DIR "output"
#define CAIRO_TEST_LOG_SUFFIX ".log"
#define CAIRO_TEST_FONT_FAMILY "DejaVu"

/* What is a fail and what isn't?
 * When running the test suite we want to detect unexpected output. This
 * can be caused by a change we have made to cairo itself, or a change
 * in our environment. To capture this we classify the expected output into 3
 * classes:
 *
 *   REF  -- Perfect output.
 *           Might be different for each backend, due to slight implementation
 *           differences.
 *
 *   NEW  -- A new failure. We have uncovered a bug within cairo and have
 *           recorded the current failure (along with the expected output
 *           if possible!) so we can detect any changes in our attempt to
 *           fix the bug.
 *
 *  XFAIL -- An external failure. We believe the cairo output is perfect,
 *           but an external renderer is causing gross failure.
 *           (We also use this to capture current WONTFIX issues within cairo,
 *           such as overflow in internal coordinates, so as not to distract
 *           us when regression testing.)
 *
 *  If no REF is given for a test, then it is assumed to be XFAIL.
 */
#define CAIRO_TEST_REF_SUFFIX ".ref"
#define CAIRO_TEST_XFAIL_SUFFIX ".xfail"
#define CAIRO_TEST_NEW_SUFFIX ".new"

#define CAIRO_TEST_OUT_SUFFIX ".out"
#define CAIRO_TEST_DIFF_SUFFIX ".diff"

#define CAIRO_TEST_PNG_EXTENSION ".png"
#define CAIRO_TEST_OUT_PNG CAIRO_TEST_OUT_SUFFIX CAIRO_TEST_PNG_EXTENSION
#define CAIRO_TEST_REF_PNG CAIRO_TEST_REF_SUFFIX CAIRO_TEST_PNG_EXTENSION
#define CAIRO_TEST_DIFF_PNG CAIRO_TEST_DIFF_SUFFIX CAIRO_TEST_PNG_EXTENSION

typedef enum cairo_test_status {
    CAIRO_TEST_SUCCESS = 0,
    CAIRO_TEST_NO_MEMORY,
    CAIRO_TEST_FAILURE,
    CAIRO_TEST_NEW,
    CAIRO_TEST_XFAILURE,
    CAIRO_TEST_ERROR,
    CAIRO_TEST_CRASHED,
    CAIRO_TEST_UNTESTED = 77 /* match automake's skipped exit status */
} cairo_test_status_t;

#define CAIRO_TEST_DOUBLE_EQUALS(a,b)  (fabs((a)-(b)) < 0.00001)

#endif
