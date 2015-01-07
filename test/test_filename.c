#include <stdio.h>

#include "../filename.c"

struct testcase {
	char *srcname;
	unsigned int framenum;
	char *pat;
	char *expect;
};

int
main ()
{
	char *out;
	int ret = 0;

	struct testcase cases[] =
	{
		{ .srcname = "cam"
		, .framenum = 123
		, .pat = "static.jpg"
		, .expect = "static.jpg"
		}
	,	{ .srcname = "cam"
		, .framenum = 123
		, .pat = "%f.jpg"
		, .expect = "123.jpg"
		}
	,	{ .srcname = "camera"
		, .framenum = 99
		, .pat = "%n"
		, .expect = "camera"
		}
	,	{ .srcname = "camz"
		, .framenum = 6
		, .pat = "%n-%f.jpg"
		, .expect = "camz-6.jpg"
		}
	,	{ .srcname = "camz"
		, .framenum = 6
		, .pat = "%n-%f-%n-%f%f.jpg%n"
		, .expect = "camz-6-camz-66.jpgcamz"
		}
	};
	for (unsigned int i = 0; i < (sizeof(cases) / sizeof(cases[0])); i++) {
		out = filename_forge(cases[i].srcname, cases[i].framenum, cases[i].pat);
		if (out == NULL) {
			printf("FAIL: out is NULL\n");
			ret = 1;
			continue;
		}
		if (strcmp(out, cases[i].expect) != 0) {
			printf("FAIL: expected %s, got %s\n", cases[i].expect, out);
			ret = 1;
		}
		free(out);
	}
	return ret;
}
