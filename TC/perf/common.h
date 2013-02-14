/*
 * Cairo Performance Test Framework
 * (c) 2012 Samsung Electronics, Inc.
 * All rights reserved.
 *
 * Measures rendering performance for image, gl backends
 *
 * This software is a confidential and proprietary information of Samsung
 * Electronics, Inc. ("Confidential Information"). You shall not disclose such
 * Confidential Information and shall use it only in accordance with the terms
 * of the license agreement you entered into with Samsung Electronics.
 *
 * Author: Dongyeon Kim <dy5.kim@samsung.com>
 */

#include <Ecore_X.h>
#include <Elementary.h>
#include <cairo.h>
#include <cairo-gl.h>

void clearCairo(cairo_t *cr, double width, double height);
void cairoSquare(cairo_t *cr, double x, double y, double length);
void cairoSquareStroke(cairo_t *cr, double x, double y, double length);
void cairoCircle(cairo_t *cr, double x, double y, double radius);
void cairoCircleStroke(cairo_t *cr, double x, double y, double radius);
void cairoTriangle(cairo_t *cr, double x, double y, double side);
void cairoTriangleStroke(cairo_t *cr, double x, double y, double side);

