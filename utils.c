/*  MiniDLNA media server
 *  Copyright (C) 2008  Justin Maggard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int
ends_with(const char * haystack, const char * needle)
{
	const char *found = strcasestr(haystack, needle);
	return (found && found[strlen(needle)] == '\0');
}

char *
trim(char *str)
{
        if (!str)
                return(NULL);
        int i;
        for (i=0; i <= strlen(str) && (isspace(str[i]) || str[i] == '"'); i++) {
		str++;
	}
        for (i=(strlen(str)-1); i >= 0 && (isspace(str[i]) || str[i] == '"'); i--) {
                str[i] = '\0';
        }
        return str;
}

char *
modifyString(char * string, const char * before, const char * after, short like)
{
	int oldlen, newlen, chgcnt = 0;
	char *s, *p, *t;

	oldlen = strlen(before);
	newlen = strlen(after);
	if( newlen > oldlen )
	{
		s = string;
		while( (p = strstr(s, before)) )
		{
			chgcnt++;
			s = p+oldlen;
		}
		string = realloc(string, strlen(string)+((newlen-oldlen)*chgcnt)+1);
	}

	s = string;
	while( s )
	{
		p = strcasestr(s, before);
		if( !p )
			return string;
		if( like )
		{
			t = p+oldlen;
			while( isspace(*t) )
				t++;
			if( *t == '"' )
				while( *++t != '"' )
					continue;
			memmove(t+1, t, strlen(t)+1);
			*t = '%';
		}
		memmove(p + newlen, p + oldlen, strlen(p + oldlen) + 1);
		memcpy(p, after, newlen);
		s = p + newlen;
	}
	if( newlen < oldlen )
		string = realloc(string, strlen(string)+1);

	return string;
}

void
strip_ext(char * name)
{
	char * period;

	period = rindex(name, '.');
	if( period )
		*period = '\0';
}
