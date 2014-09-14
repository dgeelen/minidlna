/* MiniDLNA media server
 * Copyright (C) 2008-2010  Justin Maggard
 *
 * This file is part of MiniDLNA.
 *
 * MiniDLNA is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MiniDLNA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MiniDLNA. If not, see <http://www.gnu.org/licenses/>.
 */
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifdef HAVE_INOTIFY
#include <sys/resource.h>
#include <poll.h>
#ifdef HAVE_SYS_INOTIFY_H
#include <sys/inotify.h>
#else
#include "linux/inotify.h"
#include "linux/inotify-syscalls.h"
#endif
#endif
#include "libav.h"

#include "upnpglobalvars.h"
#include "monitor.h"
#include "utils.h"
#include "sql.h"
#include "scanner.h"
#include "metadata.h"
#include "albumart.h"
#include "playlist.h"
#include "log.h"

static time_t next_pl_fill = 0;

#ifdef HAVE_INOTIFY
#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )
#define DESIRED_WATCH_LIMIT 65536

#define PATH_BUF_SIZE PATH_MAX

struct watch
{
	int wd;		/* watch descriptor */
	char *path;	/* watched path */
	struct watch *next;
};

static struct watch *watches;
static struct watch *lastwatch = NULL;

static int IsMediaPath(const char * path);

static char *
get_path_from_wd(int wd)
{
	struct watch *w = watches;

	while( w != NULL )
	{
		if( w->wd == wd )
			return w->path;
		w = w->next;
	}

	return NULL;
}

static unsigned int
next_highest(unsigned int num)
{
	num |= num >> 1;
	num |= num >> 2;
	num |= num >> 4;
	num |= num >> 8;
	num |= num >> 16;
	return ++num;
}

static void
raise_watch_limit(unsigned int limit)
{
	FILE *max_watches = fopen("/proc/sys/fs/inotify/max_user_watches", "r+");
	if (!max_watches)
		return;
	if (!limit)
		fscanf(max_watches, "%u", &limit);
	fprintf(max_watches, "%u", next_highest(limit));
	fclose(max_watches);
}

static int
add_watch(int fd, const char * path)
{
	struct watch *nw;
	int wd;

	wd = inotify_add_watch(fd, path, IN_CREATE|IN_CLOSE_WRITE|IN_DELETE|IN_MOVE);
	if( wd < 0 && errno == ENOSPC)
	{
		raise_watch_limit(0);
		wd = inotify_add_watch(fd, path, IN_CREATE|IN_CLOSE_WRITE|IN_DELETE|IN_MOVE);
	}
	if( wd < 0 )
	{
		DPRINTF(E_ERROR, L_INOTIFY, "inotify_add_watch(%s) [%s]\n", path, strerror(errno));
		return -1;
	}

	nw = malloc(sizeof(struct watch));
	if( nw == NULL )
	{
		DPRINTF(E_ERROR, L_INOTIFY, "malloc() error\n");
		return -1;
	}
	nw->wd = wd;
	nw->next = NULL;
	nw->path = strdup(path);

	if( watches == NULL )
	{
		watches = nw;
	}

	if( lastwatch != NULL )
	{
		lastwatch->next = nw;
	}
	lastwatch = nw;

	return wd;
}

static int
remove_watch(int fd, const char * path)
{
	struct watch *w;

	for( w = watches; w; w = w->next )
	{
		if( strcmp(path, w->path) == 0 )
			return(inotify_rm_watch(fd, w->wd));
	}

	return 1;
}

static int
inotify_create_watches(int fd)
{
	FILE * max_watches;
	unsigned int num_watches = 0, watch_limit;
	char **result;
	int i, rows = 0;
	struct media_dir_s * media_path;

	for( media_path = media_dirs; media_path != NULL; media_path = media_path->next )
	{
		DPRINTF(E_DEBUG, L_INOTIFY, "Add watch to %s\n", media_path->path);
		add_watch(fd, media_path->path);
		num_watches++;
	}
	sql_get_table(db, "SELECT PATH from DETAILS where MIME is NULL and PATH is not NULL", &result, &rows, NULL);
	for( i=1; i <= rows; i++ )
	{
		if(!IsMediaPath(result[i]))
		{
   		DPRINTF(E_DEBUG, L_INOTIFY, "Add watch to %s\n", result[i]);
	   	add_watch(fd, result[i]);
		   num_watches++;
		}
	}
	sqlite3_free_table(result);

	max_watches = fopen("/proc/sys/fs/inotify/max_user_watches", "r");
	if( max_watches )
	{
		if( fscanf(max_watches, "%10u", &watch_limit) < 1 )
			watch_limit = 8192;
		fclose(max_watches);
		if( (watch_limit < DESIRED_WATCH_LIMIT) || (watch_limit < (num_watches*4/3)) )
		{
			if (access("/proc/sys/fs/inotify/max_user_watches", W_OK) == 0)
			{
				if( DESIRED_WATCH_LIMIT >= (num_watches*3/4) )
				{
					raise_watch_limit(8191U);
				}
				else if( next_highest(num_watches) >= (num_watches*3/4) )
				{
					raise_watch_limit(num_watches);
				}
				else
				{
					raise_watch_limit(next_highest(num_watches));
				}
			}
			else
			{
				DPRINTF(E_WARN, L_INOTIFY, "WARNING: Inotify max_user_watches [%u] is low or close to the number of used watches [%u] "
				                        "and I do not have permission to increase this limit.  Please do so manually by "
				                        "writing a higher value into /proc/sys/fs/inotify/max_user_watches.\n", watch_limit, num_watches);
			}
		}
	}
	else
	{
		DPRINTF(E_WARN, L_INOTIFY, "WARNING: Could not read inotify max_user_watches!  "
		                        "Hopefully it is enough to cover %u current directories plus any new ones added.\n", num_watches);
	}

	return rows;
}

static int
inotify_remove_watches(int fd)
{
	struct watch *w = watches;
	struct watch *last_w;
	int rm_watches = 0;

	while( w )
	{
		last_w = w;
		inotify_rm_watch(fd, w->wd);
		free(w->path);
		rm_watches++;
		w = w->next;
		free(last_w);
	}

	return rm_watches;
}
#endif

int
monitor_remove_file(const char * path)
{
	char sql[128];
	char *id;
	char *ptr;
	char **result;
	int64_t detailID;
	int rows, playlist;

	if( is_caption(path) )
	{
		return sql_exec(db, "DELETE from CAPTIONS where PATH = '%q'", path);
	}
	/* Invalidate the scanner cache so we don't insert files into non-existent containers */
	valid_cache = 0;
	playlist = is_playlist(path);
	id = sql_get_text_field(db, "SELECT ID from %s where PATH = '%q'", playlist?"PLAYLISTS":"DETAILS", path);
	if( !id )
		return 1;
	detailID = strtoll(id, NULL, 10);
	sqlite3_free(id);
	if( playlist )
	{
		sql_exec(db, "DELETE from PLAYLISTS where ID = %lld", detailID);
		sql_exec(db, "DELETE from DETAILS where ID ="
		             " (SELECT DETAIL_ID from OBJECTS where OBJECT_ID = '%s$%llX')",
		         MUSIC_PLIST_ID, detailID);
		sql_exec(db, "DELETE from OBJECTS where OBJECT_ID = '%s$%llX' or PARENT_ID = '%s$%llX'",
		         MUSIC_PLIST_ID, detailID, MUSIC_PLIST_ID, detailID);
	}
	else
	{
		/* Delete the parent containers if we are about to empty them. */
		snprintf(sql, sizeof(sql), "SELECT PARENT_ID from OBJECTS where DETAIL_ID = %lld"
		                           " and PARENT_ID not like '64$%%'",
		                           (long long int)detailID);
		if( (sql_get_table(db, sql, &result, &rows, NULL) == SQLITE_OK) )
		{
			int i, children;
			for( i = 1; i <= rows; i++ )
			{
				/* If it's a playlist item, adjust the item count of the playlist */
				if( strncmp(result[i], MUSIC_PLIST_ID, strlen(MUSIC_PLIST_ID)) == 0 )
				{
					sql_exec(db, "UPDATE PLAYLISTS set FOUND = (FOUND-1) where ID = %d",
					         atoi(strrchr(result[i], '$') + 1));
				}

				children = sql_get_int_field(db, "SELECT count(*) from OBJECTS where PARENT_ID = '%s'", result[i]);
				if( children < 0 )
					continue;
				if( children < 2 )
				{
					sql_exec(db, "DELETE from OBJECTS where OBJECT_ID = '%s'", result[i]);

					ptr = strrchr(result[i], '$');
					if( ptr )
						*ptr = '\0';
					if( sql_get_int_field(db, "SELECT count(*) from OBJECTS where PARENT_ID = '%s'", result[i]) == 0 )
					{
						sql_exec(db, "DELETE from OBJECTS where OBJECT_ID = '%s'", result[i]);
					}
				}
			}
			sqlite3_free_table(result);
		}
		/* Now delete the actual objects */
		sql_exec(db, "DELETE from DETAILS where ID = %lld", detailID);
		sql_exec(db, "DELETE from OBJECTS where DETAIL_ID = %lld", detailID);
	}

	art_cache_cleanup(path);

	return 0;
}

int
monitor_insert_file(char * name, const char * path)
{
	int len;
	char * last_dir;
	char * path_buf;
	char * base_name;
	char * base_copy;
	char * parent_buf = NULL;
	char * id = NULL;
	int depth = 1;
	int ts;
	media_types types = ALL_MEDIA;
	struct media_dir_s * media_path = media_dirs;
	struct stat st;
	char dirpath[PATH_MAX];

	strncpyt(dirpath, path, sizeof(dirpath));
	if ( has_ignore(dirname((char*)dirpath), 1) )
		return -1;

	/* Is it cover art for another file? */
	if( is_image(path) )
		update_if_album_art(path);
	else if( is_caption(path) )
		check_for_captions(path, 0);

	/* Check if we're supposed to be scanning for this file type in this directory */
	while( media_path )
	{
		if( strncmp(path, media_path->path, strlen(media_path->path)) == 0 )
		{
			types = media_path->types;
			break;
		}
		media_path = media_path->next;
	}
	switch( types )
	{
		case ALL_MEDIA:
			if( !is_image(path) &&
			    !is_audio(path) &&
			    !is_video(path) &&
			    !is_playlist(path) )
				return -1;
			break;
		case TYPE_AUDIO:
			if( !is_audio(path) &&
			    !is_playlist(path) )
				return -1;
			break;
		case TYPE_AUDIO|TYPE_VIDEO:
			if( !is_audio(path) &&
			    !is_video(path) &&
			    !is_playlist(path) )
				return -1;
			break;
		case TYPE_AUDIO|TYPE_IMAGES:
			if( !is_image(path) &&
			    !is_audio(path) &&
			    !is_playlist(path) )
				return -1;
			break;
		case TYPE_VIDEO:
			if( !is_video(path) )
				return -1;
			break;
		case TYPE_VIDEO|TYPE_IMAGES:
			if( !is_image(path) &&
			    !is_video(path) )
				return -1;
			break;
		case TYPE_IMAGES:
			if( !is_image(path) )
				return -1;
			break;
		default:
			return -1;
	}

	/* If it's already in the database, remove it before re-inserting. */
	if( stat(path, &st) != 0 )
		return -1;

	ts = sql_get_int_field(db, "SELECT TIMESTAMP from DETAILS where PATH = '%q'", path);
	if( !ts && is_playlist(path) && (sql_get_int_field(db, "SELECT ID from PLAYLISTS where PATH = '%q'", path) > 0) )
	{
		DPRINTF(E_DEBUG, L_INOTIFY, "Re-reading modified playlist (%s).\n", path);
		monitor_remove_file(path);
		next_pl_fill = 1;
	}
	else if( !ts )
	{
		DPRINTF(E_DEBUG, L_INOTIFY, "Adding: %s\n", path);
	}
	else if( ts != st.st_mtime )
	{
		DPRINTF(E_DEBUG, L_INOTIFY, "%s is %s than the last db entry.\n",
			path, (ts < st.st_mtime) ? "older" : "newer");
		monitor_remove_file(path);
	}
	else
	{
		if( ts == st.st_mtime )
			DPRINTF(E_DEBUG, L_INOTIFY, "%s already exists\n", path);
		return 0;
	}

	/* Find the parentID.  If it's not found, create all necessary parents. */
	len = strlen(path)+1;
	if( !(path_buf = malloc(len)) ||
	    !(last_dir = malloc(len)) ||
	    !(base_name = malloc(len)) )
		return -1;
	base_copy = base_name;
	while( depth )
	{
		depth = 0;
		strcpy(path_buf, path);
		parent_buf = dirname(path_buf);

		do
		{
			//DEBUG DPRINTF(E_DEBUG, L_INOTIFY, "Checking %s\n", parent_buf);
			id = sql_get_text_field(db, "SELECT OBJECT_ID from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
			                            " where d.PATH = '%q' and REF_ID is NULL", parent_buf);
			if( id )
			{
				if( !depth )
					break;
				DPRINTF(E_DEBUG, L_INOTIFY, "Found first known parentID: %s [%s]\n", id, parent_buf);
				/* Insert newly-found directory */
				strcpy(base_name, last_dir);
				base_copy = basename(base_name);
				insert_directory(base_copy, last_dir, BROWSEDIR_ID, id+2, get_next_available_id("OBJECTS", id));
				sqlite3_free(id);
				break;
			}
			depth++;
			strcpy(last_dir, parent_buf);
			parent_buf = dirname(parent_buf);
		}
		while( strcmp(parent_buf, "/") != 0 );

		if( strcmp(parent_buf, "/") == 0 )
		{
			id = sqlite3_mprintf("%s", BROWSEDIR_ID);
			depth = 0;
			break;
		}
		strcpy(path_buf, path);
	}
	free(last_dir);
	free(path_buf);
	free(base_name);

	if( !depth )
	{
		//DEBUG DPRINTF(E_DEBUG, L_INOTIFY, "Inserting %s\n", name);
		insert_file(name, path, id+2, get_next_available_id("OBJECTS", id), types);
		sqlite3_free(id);
		if( (is_audio(path) || is_playlist(path)) && next_pl_fill != 1 )
		{
			next_pl_fill = time(NULL) + 120; // Schedule a playlist scan for 2 minutes from now.
			//DEBUG DPRINTF(E_WARN, L_INOTIFY,  "Playlist scan scheduled for %s", ctime(&next_pl_fill));
		}
		if( is_video(path))
			GenerateMTA(path);
	}
	return depth;
}

int
monitor_insert_directory(int fd, char *name, const char * path)
{
	DIR * ds;
	struct dirent * e;
	char *id, *parent_buf, *esc_name;
	char path_buf[PATH_MAX];
	enum file_types type = TYPE_UNKNOWN;
	media_types dir_types = ALL_MEDIA;
	struct media_dir_s* media_path;
	struct stat st;

	if( access(path, R_OK|X_OK) != 0 )
	{
		DPRINTF(E_WARN, L_INOTIFY, "Could not access %s [%s]\n", path, strerror(errno));
		return -1;
	}
	if( sql_get_int_field(db, "SELECT ID from DETAILS where PATH = '%q'", path) > 0 )
	{
		fd = 0;
		DPRINTF(E_DEBUG, L_INOTIFY, "%s already exists\n", path);
	}
	else
	{
		parent_buf = strdup(path);
		id = sql_get_text_field(db, "SELECT OBJECT_ID from OBJECTS o left join DETAILS d on (d.ID = o.DETAIL_ID)"
					    " WHERE d.PATH = '%q' and REF_ID is NULL", dirname(parent_buf));
		if( !id )
			id = sqlite3_mprintf("%s", BROWSEDIR_ID);
		insert_directory(name, path, BROWSEDIR_ID, id+2, get_next_available_id("OBJECTS", id));
		sqlite3_free(id);
		free(parent_buf);
	}

	if( fd > 0 )
	{
		#ifdef HAVE_INOTIFY
		int wd = add_watch(fd, path);
		if( wd == -1 )
		{
			DPRINTF(E_ERROR, L_INOTIFY, "add_watch() failed\n");
		}
		else
		{
			DPRINTF(E_INFO, L_INOTIFY, "Added watch to %s [%d]\n", path, wd);
		}
		#endif
	}

	media_path = media_dirs;
	while( media_path )
	{
		if( strncmp(path, media_path->path, strlen(media_path->path)) == 0 )
		{
			dir_types = media_path->types;
			break;
		}
		media_path = media_path->next;
	}

	ds = opendir(path);
	if( !ds )
	{
		DPRINTF(E_ERROR, L_INOTIFY, "opendir failed! [%s]\n", strerror(errno));
		return -1;
	}
	while( (e = readdir(ds)) )
	{
		if( e->d_name[0] == '.' )
			continue;
		esc_name = escape_tag(e->d_name, 1);
		snprintf(path_buf, sizeof(path_buf), "%s/%s", path, e->d_name);
		switch( e->d_type )
		{
			case DT_DIR:
			case DT_REG:
			case DT_LNK:
			case DT_UNKNOWN:
				type = resolve_unknown_type(path_buf, dir_types);
			default:
				break;
		}
		if( type == TYPE_DIR )
		{
			monitor_insert_directory(fd, esc_name, path_buf);
		}
		else if( type == TYPE_FILE )
		{
			if( (stat(path_buf, &st) == 0) && (st.st_blocks<<9 >= st.st_size) )
			{
				monitor_insert_file(esc_name, path_buf);
			}
		}
		free(esc_name);
	}
	closedir(ds);

	return 0;
}

static int
IsMediaPath(const char * path)
{
  struct media_dir_s * current = media_dirs;
  while(current != NULL)
  {
    if(strcmp(path, current->path) == 0)
    {
      return 1;
    }
    current = current->next;
  }
  return 0;
}

int
monitor_remove_directory(int fd, const char * path)
{
	char * sql;
	char **result;
	int64_t detailID = 0;
	int rows, i, ret = 1;

	/* Invalidate the scanner cache so we don't insert files into non-existent containers */
	valid_cache = 0;
	if( fd > 0 )
	{
		#ifdef HAVE_INOTIFY
		remove_watch(fd, path);
		#endif
	}
	sql = sqlite3_mprintf("SELECT ID, PATH"
	                      " from DETAILS where (PATH > '%q/' and PATH <= '%q/%c')"
	                      " or PATH = '%q'", path, path, 0xFF, path);
	if( (sql_get_table(db, sql, &result, &rows, NULL) == SQLITE_OK) )
	{
		for(i=2; i <= 2*rows; i+=2) // x2 since we've asked for 2 columns
		{
			detailID = strtoll(result[i], NULL, 10);
			sql_exec(db, "DELETE from ALBUM_ART where ID = (SELECT ALBUM_ART from DETAILS where ID = %lld)", detailID);
			sql_exec(db, "DELETE from DETAILS where ID = %lld", detailID);
			sql_exec(db, "DELETE from OBJECTS where DETAIL_ID = %lld", detailID);
			art_cache_cleanup(result[i+1]);
		}
		ret = 0;
		sqlite3_free_table(result);
	}
	sqlite3_free(sql);
	/* Clean up any album art entries in the deleted directory */
	sql_exec(db, "DELETE from ALBUM_ART where (PATH > '%q/' and PATH <= '%q/%c' or PATH = '%q')", path, path, 0xFF, path);

	return ret;
}

#ifdef HAVE_INOTIFY
void *
start_inotify(void)
{
	struct pollfd pollfds[1];
	int timeout = 1000;
	char buffer[BUF_LEN];
	char path_buf[PATH_MAX];
	int length, i = 0;
	char * esc_name = NULL;
	struct stat st;
#ifdef ENABLE_VIDEO_THUMB
	char renpath_buf[PATH_MAX] = {};
	int cookie = 0;
#endif
	sigset_t set;

	sigfillset(&set);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	pollfds[0].fd = inotify_init();
	pollfds[0].events = POLLIN;

	if ( pollfds[0].fd < 0 )
		DPRINTF(E_ERROR, L_INOTIFY, "inotify_init() failed!\n");

	while( scanning )
	{
		if( quitting )
			goto quitting;
		sleep(1);
	}
	inotify_create_watches(pollfds[0].fd);
	if (setpriority(PRIO_PROCESS, 0, 19) == -1)
		DPRINTF(E_WARN, L_INOTIFY,  "Failed to reduce inotify thread priority\n");
	sqlite3_release_memory(1<<31);

	av_register_all();
#ifdef ENABLE_VIDEO_THUMB
	avcodec_register_all();
#endif
	av_log_set_level(AV_LOG_PANIC);

	while( !quitting )
	{
		length = poll(pollfds, 1, timeout);
		if( !length )
		{
			if( next_pl_fill && (time(NULL) >= next_pl_fill) )
			{
				fill_playlists();
				next_pl_fill = 0;
			}
			continue;
		}
		else if( length < 0 )
		{
			if( (errno == EINTR) || (errno == EAGAIN) )
				continue;
			else
				DPRINTF(E_ERROR, L_INOTIFY, "read failed!\n");
		}
		else
		{
			length = read(pollfds[0].fd, buffer, BUF_LEN);
			buffer[BUF_LEN-1] = '\0';
		}

		i = 0;
		while( i < length )
		{
			struct inotify_event * event = (struct inotify_event *) &buffer[i];
			if( event->len )
			{
				if( *(event->name) == '.' )
				{
					i += EVENT_SIZE + event->len;
					continue;
				}
				esc_name = modifyString(strdup(event->name), "&", "&amp;amp;", 0);
				snprintf(path_buf, sizeof(path_buf), "%s/%s", get_path_from_wd(event->wd), event->name);
#ifdef ENABLE_VIDEO_THUMB
				DPRINTF(E_DEBUG, L_INOTIFY,  "%s '%s' was %s (%x).\n",
					path_buf,
					(event->mask & IN_ISDIR     ) ? "directory" : "file",
					(event->mask & IN_MOVED_TO  ) ? "moved here" :
					(event->mask & IN_MOVED_FROM) ? "moved away" :
					(event->mask & IN_DELETE    ) ? "deleted" :
					(event->mask & IN_CREATE    ) ? "created" :
					(event->mask & IN_CLOSE     ) ? "closed" :
					"other",
					event->mask
				);
				/* We do not want to regenerate the thumbnails if renaming a directory. */
				if (event->cookie == cookie && event->mask & IN_MOVED_TO)
				{
					DPRINTF(E_DEBUG, L_INOTIFY, "Detected rename: '%s' -> '%s'\n", renpath_buf+1, path_buf);
					art_cache_rename(renpath_buf+1, path_buf);
				}
				else if(renpath_buf[0]) { // check for delayed action
					DPRINTF(E_DEBUG, L_INOTIFY, "Delayed delete for: '%s'\n", renpath_buf+1);
					if( renpath_buf[0] == 'd' )
					{
						monitor_remove_directory(pollfds[0].fd, renpath_buf+1);
					}
					else
						monitor_remove_file(renpath_buf+1);
				}
				// Clear any delayed action (either it was a rename, or it has been
				// executed just now)
				renpath_buf[0] = 0;
#endif
				if ( event->mask & IN_ISDIR && (event->mask & (IN_CREATE|IN_MOVED_TO)) )
				{
					monitor_insert_directory(pollfds[0].fd, esc_name, path_buf);
				}
				else if ( (event->mask & (IN_CLOSE_WRITE|IN_MOVED_TO|IN_CREATE)) &&
				          (lstat(path_buf, &st) == 0) )
				{
					if( (event->mask & (IN_MOVED_TO|IN_CREATE)) && (S_ISLNK(st.st_mode) || st.st_nlink > 1) )
					{
						DPRINTF(E_DEBUG, L_INOTIFY, "The %s link %s was %s.\n",
							(S_ISLNK(st.st_mode) ? "symbolic" : "hard"),
							path_buf, (event->mask & IN_MOVED_TO ? "moved here" : "created"));
						if( stat(path_buf, &st) == 0 && S_ISDIR(st.st_mode) )
							monitor_insert_directory(pollfds[0].fd, esc_name, path_buf);
						else
							monitor_insert_file(esc_name, path_buf);
					}
					else if( event->mask & (IN_CLOSE_WRITE|IN_MOVED_TO) && st.st_size > 0 )
					{
						if( (event->mask & IN_MOVED_TO) ||
						    (sql_get_int_field(db, "SELECT TIMESTAMP from DETAILS where PATH = '%q'", path_buf) != st.st_mtime) )
						{
							DPRINTF(E_INFO, L_INOTIFY, "The file %s was %s.\n",
								path_buf, (event->mask & IN_MOVED_TO ? "moved here" : "changed"));
							monitor_insert_file(esc_name, path_buf);
						}
					}
				}
				else if ( event->mask & (IN_DELETE|IN_MOVED_FROM) )
				{
#ifdef ENABLE_VIDEO_THUMB
					if ( event->mask & IN_MOVED_FROM )
					{
						// action will be taken on the next event
						// this is to avoid deleting and having to regenerate thumbnails
						// in case of just a rename.
						strncpy(renpath_buf+1, path_buf, sizeof(renpath_buf)-1);
						renpath_buf[0] = (event->mask & IN_ISDIR) ? 'd' : 'f';
						cookie = event->cookie;
					}
					else {
#endif
					if ( event->mask & IN_ISDIR )
					{
						monitor_remove_directory(pollfds[0].fd, path_buf);
					}
					else
						monitor_remove_file(path_buf);
#ifdef ENABLE_VIDEO_THUMB
					}
#endif
				}
				free(esc_name);
			}
			i += EVENT_SIZE + event->len;
		}
	}
	inotify_remove_watches(pollfds[0].fd);

quitting:
	if(renpath_buf[0]) {
		DPRINTF(E_DEBUG, L_INOTIFY, "Delayed delete for: '%s'\n", renpath_buf+1);
		if( renpath_buf[0] == 'd' )
		{
			monitor_remove_directory(pollfds[0].fd, renpath_buf+1);
		}
		else
			monitor_remove_file(renpath_buf+1);
	}
	close(pollfds[0].fd);

	return 0;
}
#endif
