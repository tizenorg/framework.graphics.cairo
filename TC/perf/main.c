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
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cairo.h>
#include <cairo-gl.h>

#define SURFACE_TYPE_IMAGE  0
#define SURFACE_TYPE_GL     1
#define TOTAL_TIME      100

int WIDTH, HEIGHT;

//Ecore Evas variables
Ecore_X_Window window;
Evas_Object *img;

//EGL variables
EGLDisplay egl_display;
EGLSurface egl_surface;
EGLContext egl_context;

//Cairo variables
cairo_device_t *cairo_device;
cairo_surface_t *cairo_surface;
cairo_t *cr;

Eina_Bool renderMain(void *data)
{
    static int counter = 0;
    static float totalTime = 0;
    static float totalPaint = 0;
    static float totalUpdate = 0;
    struct timeval paintStart, paintStop, updateStop;

    cairo_save(cr);
    gettimeofday(&paintStart, NULL);
    /* ########## PAINT : START ########## */
    render(cr);
    /* ########## PAINT : END ########## */
    gettimeofday(&paintStop, NULL);
    /* ########## UPDATE : START ########## */
    if(cairo_surface_get_type(cairo_get_target(cr)) == CAIRO_SURFACE_TYPE_GL) {
        cairo_gl_surface_swapbuffers(cairo_get_target(cr));
    } else {
        unsigned char *imageData = cairo_image_surface_get_data(cairo_get_target(cr));
        evas_object_image_data_set(img, imageData);
        evas_object_image_data_update_add(img, 0, 0, WIDTH, HEIGHT);
        ecore_x_sync();
    }
    /* ########## UPDATE : END ########## */
    gettimeofday(&updateStop, NULL);
    cairo_restore(cr);

    totalTime += updateStop.tv_usec - paintStart.tv_usec;
    totalTime += (updateStop.tv_sec - paintStart.tv_sec)*1000000;
    totalPaint += (paintStop.tv_usec - paintStart.tv_usec);
    totalPaint += (paintStop.tv_sec - paintStart.tv_sec)*1000000;
    totalUpdate += (updateStop.tv_usec - paintStop.tv_usec);
    totalUpdate += (updateStop.tv_sec - paintStop.tv_sec)*1000000;
    counter++;

    if(counter == TOTAL_TIME)
    {
        float fps =  TOTAL_TIME / totalTime * 1000000.0f;
        printf("fps = %0.2f\n", fps);
        printf("average paint time = %0.1f usec, update time = %0.1f usec\n", totalPaint/TOTAL_TIME, totalUpdate/TOTAL_TIME);

        elm_exit();
        return 0;
    }
}

void initELMWindow(int surface_type)
{
    Evas_Object *win = elm_win_add(NULL, "cairotest", ELM_WIN_BASIC);
    elm_win_autodel_set(win, EINA_TRUE);
    ecore_x_screen_size_get(ecore_x_default_screen_get(), &WIDTH, &HEIGHT);
    evas_object_resize(win, WIDTH, HEIGHT);
    evas_object_show(win);

    if (surface_type == SURFACE_TYPE_IMAGE) {
        Evas_Object *img_win = elm_image_add(win);
        img = evas_object_image_filled_add(evas_object_evas_get(img_win));
        elm_win_resize_object_add(win, img);
        evas_object_image_content_hint_set(img, EVAS_IMAGE_CONTENT_HINT_DYNAMIC);
        evas_object_image_size_set(img, WIDTH, HEIGHT);
        evas_object_image_colorspace_set(img, EVAS_COLORSPACE_ARGB8888);
        evas_object_image_alpha_set(img, 0);
        evas_object_show(img_win);
        evas_object_show(img);
    } else if(surface_type == SURFACE_TYPE_GL) {
        window = ecore_x_window_new(0, 0, 0, WIDTH, HEIGHT);
        ecore_x_icccm_title_set(window, "window");
        ecore_x_netwm_name_set(window, "window");
        ecore_x_input_multi_select(window);
        ecore_x_icccm_transient_for_set(window, elm_win_xwindow_get(win));
        ecore_x_window_show(window);
    }
}

void initEGL(int surface_type)
{
    if(surface_type != SURFACE_TYPE_GL)
        return;

    setenv("ELM_ENGINE", "gl", 1);

    egl_display = eglGetDisplay((EGLNativeDisplayType) ecore_x_display_get());
    if(egl_display == EGL_NO_DISPLAY)
    {
        printf("cannot get egl display\n");
        exit(1);
    }

    EGLint major, minor;
    if(!eglInitialize(egl_display, &major, &minor))
    {
        printf("cannot initialize egl\n");
        exit(-1);
    }

    if(!eglBindAPI(EGL_OPENGL_ES_API))
    {
        printf("cannot bind egl to gles2 API\n");
        exit(-1);
    }

    EGLConfig egl_config;
    EGLint num;

    EGLint attr[] =
    {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_STENCIL_SIZE, 0,
        EGL_SAMPLES, 4,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };


    if(!eglChooseConfig(egl_display, attr, &egl_config, 1, &num))
    {
        printf("cannot choose config\n");
        exit(-1);
    }

    if(num != 1)
    {
        printf("did not get exactly one config = %d\n", num);
        exit(-1);
    }

    egl_surface = eglCreateWindowSurface(egl_display,
        egl_config, (NativeWindowType) window, NULL);
    if(egl_surface == EGL_NO_SURFACE)
    {
        printf("cannot create surface\n");
        exit(-1);
    }

    EGLint e = eglGetError();
    //printf("egl error = %x\n", e);

    EGLint ctxattr[] =
    {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, ctxattr);
    if(egl_context == EGL_NO_CONTEXT)
    {
        EGLint e = eglGetError();
        printf("cannot create context, error = %x\n", e);
        exit(-1);
    }

    EGLint value;
    EGLBoolean result = eglQueryContext(egl_display, egl_context, EGL_CONTEXT_CLIENT_VERSION, &value);
    //printf("Context version = %x, result = %d\n", value, result);

    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
}

void destroyEGL(int surface_type)
{
    if(surface_type != SURFACE_TYPE_GL)
        return;

    eglDestroyContext(egl_display, egl_context);
    eglDestroySurface(egl_display, egl_surface);
    eglTerminate(egl_display);
}

void initCairo(int surface_type)
{
    if(surface_type == SURFACE_TYPE_IMAGE) {
        printf("== CREATE IMAGE SURFACE ==\n");
        cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, WIDTH, HEIGHT);
    } else if(surface_type == SURFACE_TYPE_GL) {
        printf("== CREATE GL SURFACE ==\n");
        setenv("CAIRO_GL_COMPOSITOR", "msaa", 1);
        cairo_device = cairo_egl_device_create(egl_display, egl_context);
        cairo_gl_device_set_thread_aware(cairo_device, 0);
        cairo_surface = cairo_gl_surface_create_for_egl(cairo_device, egl_surface, WIDTH, HEIGHT);
    }
    cr = cairo_create(cairo_surface);
}

void destroyCairo(int surface_type)
{
    cairo_surface_destroy(cairo_surface);
    cairo_destroy(cr);

    if(surface_type == SURFACE_TYPE_GL) {
        cairo_device_destroy(cairo_device);
    }
}

int main(int argc, char **argv)
{
    int surface_type = SURFACE_TYPE_GL;

    if(argc == 2)
        surface_type = atoi(argv[1]);

    elm_init(argc, argv);

    initELMWindow(surface_type);
    initEGL(surface_type);
    initCairo(surface_type);

    preRender(cr);
    Ecore_Animator *animator = ecore_animator_add(renderMain, (void *)cr);
    elm_run();
    postRender(cr);

    destroyCairo(surface_type);
    destroyEGL(surface_type);
    elm_shutdown();

    return 0;
}

