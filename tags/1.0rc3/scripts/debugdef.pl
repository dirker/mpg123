#!/usr/bin/perl
# debugdef.pl: avoid variadic debugging macros through automatic definitions

# written by Thomas Orgis <thomas@orgis.org>, placed in the public domain

my $num = shift(@ARGV);

print <<EOT;
/*
	debug.h: 
		if DEBUG defined: debugging macro fprintf wrappers
		else: macros defined to do nothing
	That saves typing #ifdef DEBUG all the time and still preserves
	lean code without debugging.
	
	public domain (or LGPL / GPL, if you like that more;-)
	generated by debugdef.pl, what was
	trivially written by Thomas Orgis <thomas\@orgis.org>
*/

#include "config.h"

/*
	I could do that with variadic macros available:
	#define sdebug(me, s) fprintf(stderr, "[location] " s "\\n")
	#define debug(me, s, ...) fprintf(stderr, "[location] " s "\}n", __VA_ARGS__)

	Variadic macros are a C99 feature...
	Now just predefining stuff non-variadic for up to $num arguments.
	It's cumbersome to have them all with different names, though...
*/

#ifdef DEBUG
#include <stdio.h>
EOT
printdefs(1);
print "#else\n";
printdefs(0);
print "#endif\n";

for('warning', 'error', 'ereturn')
{
	print "\n/* $_ macros also here... */\n";
	printdefs(1, $_);
}

sub printdefs
{
	my $forreal = shift;
	my $type = shift;
	$type = 'debug' unless defined $type;
	my $i;
	my $pre = ''; my $post = ''; my $rv = '';
	if($type eq 'ereturn')
	{
		$pre  = 'do{ ';
		$post = '; return rv; }while(0)';
		$rv   = 'rv, ';
	}
	while(++$i <= $num+1)
	{
		my @args, my $j;
		while(++$j < $i){ push(@args, chr(ord('a')+$j-1)); }
		unshift(@args, '') if(@args);
		print '#define '.$type.($i > 1 ? ($i-1) : '').'('.$rv.'s';
		print join(', ', @args).') ';
		if($forreal){ print $pre.'fprintf(stderr, "[" __FILE__ ":%i] '.$type.': " s "\n", __LINE__'.join(', ', @args).")$post\n"; }
		#else{ print "do {} while(0)\n"; } 
		else{ print "\n"; }
	}
}

