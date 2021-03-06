/*
 * ieee1394.c
 *
 * The ieee1394 functions
 *
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation version 2 of the License.
 * 
 *	This program is distributed in the hope that it will be useful, but
 *	WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *	General Public License for more details.
 * 
 *	You should have received a copy of the GNU General Public License along
 *	with this program; if not, write to the Free Software Foundation, Inc.,
 *	675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stddef.h>	/* for NULL */
#include <stdlib.h>	/* for getenv() */
#include <string.h>	/* for strtoul() */
#include <errno.h>
#include "hotplug.h"

#include "ieee1394_modules.h"

/* bitmap values taken from drivers/ieee1394/ieee1394_hotplug.h */
#define IEEE1394_MATCH_VENDOR_ID	0x0001
#define IEEE1394_MATCH_MODEL_ID		0x0002
#define IEEE1394_MATCH_SPECIFIER_ID	0x0004
#define IEEE1394_MATCH_VERSION		0x0008


static int match (unsigned int vendor_id, unsigned int specifier_id, unsigned int version)
{
	int i;
	int retval;

	dbg ("vendor_id = %x, specifier_id = %x, version = %x",
	     vendor_id, specifier_id, version);

	for (i = 0; ieee1394_module_map[i].module_name != NULL; ++i) {
		dbg ("looking at %s, match_flags = %x",
		     ieee1394_module_map[i].module_name,
		     ieee1394_module_map[i].match_flags);
		if ((ieee1394_module_map[i].match_flags & IEEE1394_MATCH_VENDOR_ID) &&
		    (ieee1394_module_map[i].vendor_id != vendor_id)) {
			dbg ("vendor check failed %x != %x",
			     ieee1394_module_map[i].vendor_id,
			     vendor_id);
			continue;
		}
		if ((ieee1394_module_map[i].match_flags & IEEE1394_MATCH_SPECIFIER_ID) &&
		    (ieee1394_module_map[i].specifier_id != specifier_id)) {
			dbg ("specifier_id check failed %x != %x",
			     ieee1394_module_map[i].specifier_id,
			     specifier_id);
			continue;
		}
		if ((ieee1394_module_map[i].match_flags & IEEE1394_MATCH_VERSION) &&
		    (ieee1394_module_map[i].version != version)) {
			dbg ("version check failed %x != %x",
			     ieee1394_module_map[i].version,
			     version);
			continue;
		}
		/* found one! */
		dbg ("loading %s", ieee1394_module_map[i].module_name);
		retval = load_module (ieee1394_module_map[i].module_name);
		if (retval)
			return retval;
	}

	return -ENODEV;
}


static int ieee1394_add (void)
{
	char *vendor_env;
	char *specifier_env;
	char *version_env;
	int error;
	unsigned int vendor_id;
	unsigned int specifier_id;
	unsigned int version;
	
	vendor_env = getenv ("VENDOR_ID");
	specifier_env = getenv ("SPECIFIER_ID");
	version_env = getenv ("VERSION");
	if ((vendor_env == NULL) ||
	    (specifier_env == NULL) ||
	    (version_env == NULL)) {
		dbg ("missing an environment variable, aborting.");
		return 1;
	}
	vendor_id = strtoul (vendor_env, NULL, 16);
	specifier_id = strtoul (specifier_env, NULL, 16);
	version = strtoul (version_env, NULL, 16);

#ifdef WAIT_MODULE_DIR
	wait_dir_ready("/lib/modules/2.6.12.6-VENUS/kernel", 100);
	usleep(300000);
#endif
	error = match (vendor_id, specifier_id, version);

	return error;
}


static int ieee1394_remove (void)
{
	/* right now we don't do anything here :) */
	return 0;
}


static struct subsystem ieee1394_subsystem[] = {
	{ ADD_STRING, ieee1394_add },
	{ REMOVE_STRING, ieee1394_remove },
	{ NULL, NULL }
};


int ieee1394_handler (void)
{
	char * action;
	
	action = getenv ("ACTION");
	dbg ("action = %s", action);
	if (action == NULL) {
		dbg ("missing ACTION environment variable, aborting.");
		return 1;
	}

	return call_subsystem (action, ieee1394_subsystem);
}

