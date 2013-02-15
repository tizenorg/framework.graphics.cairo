#include <tet_api.h>
#include <cairo/cairo.h>
#include <cairo/cairo-script-interpreter.h>

#include "cairo-test.h"

#include <unistd.h>
#include <sys/wait.h>

static void startup(void);
static void cleanup(void);

void (*tet_startup)(void) = startup;
void (*tet_cleanup)(void) = cleanup;

static void utc_cairo_text_antialias_subpixel_bgr1(void);

struct tet_testlist tet_testlist[] = {
	{ utc_cairo_text_antialias_subpixel_bgr1, 1 },
	{ NULL, 0 },
};

static void startup(void)
{
	/* start of TC */
}

static void cleanup(void)
{
	/* end of TC */
}

static void utc_cairo_text_antialias_subpixel_bgr1(void)
{
        char buf[128];
        int ret;
        sprintf(buf, "cd %s && ./cairo-test-suite text-antialias-subpixel-bgr", getenv("CAIRO_TC_ROOT_PATH"));
	int pid=0;
	int status=0;

	pid = fork();
	if(pid > 0) {
		if (waitpid(pid,&status,0) != pid) {
			fprintf(stderr, "Failed to wait!!!");
			exit(EXIT_FAILURE);
		}
		if(WIFEXITED(status)) {
			ret=status;
			if(WEXITSTATUS(ret) == CAIRO_TEST_SUCCESS)
				dts_pass("utc_cairo_text_antialias_subpixel_bgr1");
			else
				dts_fail("utc_cairo_text_antialias_subpixel_bgr1");
		}
	}
	else if(pid == 0) {
		char *env[]={"CAIRO_TEST_TARGET=egl","DISPLAY=:0", "CAIRO_GL_COMPOSITOR=msaa", (char *)0};
		char parse0[4]={0,};
		char parse1[1024]={0,};
		char parse2[4]={0,};
		char parse3[32]={0,};
		char parse4[32]={0,};
		sscanf(buf,"%s %s %s %s %s", parse0, parse1, parse2, parse3, parse4 );
		chdir(parse1);
		execle(parse3, parse3, parse4, NULL, env);
	}
	else {
		fprintf(stderr, "Failed to fork!!!");
		exit(EXIT_FAILURE);
	}
}
