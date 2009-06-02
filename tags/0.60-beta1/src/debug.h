/*
	debug.h: 
		if DEBUG defined: debugging macro fprintf wrappers
		else: macros defined to do nothing
	That saves typing #ifdef DEBUG all the time and still preserves
	lean code without debugging.
	
	public domain (or LGPL / GPL, if you like that more;-)
	generated by debugdef.pl, what was
	trivially written by Thomas Orgis <thomas@orgis.org>
*/

#include "config.h"

/*
	I could do that with variadic macros available:
	#define sdebug(me, s) fprintf(stderr, "[" me "] " s "
")
	#define debug(me, s, ...) fprintf(stderr, "[" me "] " s "
", __VA_ARGS__)

	Variadic macros are a C99 feature...
	Now just predefining stuff non-variadic for up to 10 arguments.
	It's cumbersome to have them all with different names, though...
*/

#ifdef DEBUG
	#include <stdio.h>
	#define debug(s) fprintf(stderr, "[" __FILE__ ":%i] " s "\n", __LINE__)
	#define debug1(s, a) fprintf(stderr, "[" __FILE__ ":%i] " s "\n", __LINE__, a)
	#define debug2(s, a, b) fprintf(stderr, "[" __FILE__ ":%i] " s "\n", __LINE__, a, b)
	#define debug3(s, a, b, c) fprintf(stderr, "[" __FILE__ ":%i] " s "\n", __LINE__, a, b, c)
	#define debug4(s, a, b, c, d) fprintf(stderr, "[" __FILE__ ":%i] " s "\n", __LINE__, a, b, c, d)
	#define debug5(s, a, b, c, d, e) fprintf(stderr, "[" __FILE__ ":%i] " s "\n", __LINE__, a, b, c, d, e)
	#define debug6(s, a, b, c, d, e, f) fprintf(stderr, "[" __FILE__ ":%i] " s "\n", __LINE__, a, b, c, d, e, f)
	#define debug7(s, a, b, c, d, e, f, g) fprintf(stderr, "[" __FILE__ ":%i] " s "\n", __LINE__, a, b, c, d, e, f, g)
	#define debug8(s, a, b, c, d, e, f, g, h) fprintf(stderr, "[" __FILE__ ":%i] " s "\n", __LINE__, a, b, c, d, e, f, g, h)
	#define debug9(s, a, b, c, d, e, f, g, h, i) fprintf(stderr, "[" __FILE__ ":%i] " s "\n", __LINE__, a, b, c, d, e, f, g, h, i)
	#define debug10(s, a, b, c, d, e, f, g, h, i, j) fprintf(stderr, "[" __FILE__ ":%i] " s "\n", __LINE__, a, b, c, d, e, f, g, h, i, j)
#else
	#define debug(s) {}
	#define debug1(s, a) {}
	#define debug2(s, a, b) {}
	#define debug3(s, a, b, c) {}
	#define debug4(s, a, b, c, d) {}
	#define debug5(s, a, b, c, d, e) {}
	#define debug6(s, a, b, c, d, e, f) {}
	#define debug7(s, a, b, c, d, e, f, g) {}
	#define debug8(s, a, b, c, d, e, f, g, h) {}
	#define debug9(s, a, b, c, d, e, f, g, h, i) {}
	#define debug10(s, a, b, c, d, e, f, g, h, i, j) {}
#endif