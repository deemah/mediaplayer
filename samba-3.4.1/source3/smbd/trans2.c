/*
   Unix SMB/CIFS implementation.
   SMB transaction2 handling
   Copyright (C) Jeremy Allison			1994-2007
   Copyright (C) Stefan (metze) Metzmacher	2003
   Copyright (C) Volker Lendecke		2005-2007
   Copyright (C) Steve French			2005
   Copyright (C) James Peach			2006-2007

   Extensively modified by Andrew Tridgell, 1995

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include "version.h"
#include "smbd/globals.h"

extern enum protocol_types Protocol;

#define DIR_ENTRY_SAFETY_MARGIN 4096

static char *store_file_unix_basic(connection_struct *conn,
				char *pdata,
				files_struct *fsp,
				const SMB_STRUCT_STAT *psbuf);

static char *store_file_unix_basic_info2(connection_struct *conn,
				char *pdata,
				files_struct *fsp,
				const SMB_STRUCT_STAT *psbuf);

/********************************************************************
 Roundup a value to the nearest allocation roundup size boundary.
 Only do this for Windows clients.
********************************************************************/

uint64_t smb_roundup(connection_struct *conn, uint64_t val)
{
	uint64_t rval = lp_allocation_roundup_size(SNUM(conn));

	/* Only roundup for Windows clients. */
	enum remote_arch_types ra_type = get_remote_arch();
	if (rval && (ra_type != RA_SAMBA) && (ra_type != RA_CIFSFS)) {
		val = SMB_ROUNDUP(val,rval);
	}
	return val;
}

/****************************************************************************
 Utility functions for dealing with extended attributes.
****************************************************************************/

/****************************************************************************
 Refuse to allow clients to overwrite our private xattrs.
****************************************************************************/

static bool samba_private_attr_name(const char *unix_ea_name)
{
	static const char * const prohibited_ea_names[] = {
		SAMBA_POSIX_INHERITANCE_EA_NAME,
		SAMBA_XATTR_DOS_ATTRIB,
		NULL
	};

	int i;

	for (i = 0; prohibited_ea_names[i]; i++) {
		if (strequal( prohibited_ea_names[i], unix_ea_name))
			return true;
	}
	if (StrnCaseCmp(unix_ea_name, SAMBA_XATTR_DOSSTREAM_PREFIX,
			strlen(SAMBA_XATTR_DOSSTREAM_PREFIX)) == 0) {
		return true;
	}
	return false;
}

/****************************************************************************
 Get one EA value. Fill in a struct ea_struct.
****************************************************************************/

NTSTATUS get_ea_value(TALLOC_CTX *mem_ctx, connection_struct *conn,
		      files_struct *fsp, const char *fname,
		      const char *ea_name, struct ea_struct *pea)
{
	/* Get the value of this xattr. Max size is 64k. */
	size_t attr_size = 256;
	char *val = NULL;
	ssize_t sizeret;

 again:

	val = TALLOC_REALLOC_ARRAY(mem_ctx, val, char, attr_size);
	if (!val) {
		return NT_STATUS_NO_MEMORY;
	}

	if (fsp && fsp->fh->fd != -1) {
		sizeret = SMB_VFS_FGETXATTR(fsp, ea_name, val, attr_size);
	} else {
		sizeret = SMB_VFS_GETXATTR(conn, fname, ea_name, val, attr_size);
	}

	if (sizeret == -1 && errno == ERANGE && attr_size != 65536) {
		attr_size = 65536;
		goto again;
	}

	if (sizeret == -1) {
		return map_nt_error_from_unix(errno);
	}

	DEBUG(10,("get_ea_value: EA %s is of length %u\n", ea_name, (unsigned int)sizeret));
	dump_data(10, (uint8 *)val, sizeret);

	pea->flags = 0;
	if (strnequal(ea_name, "user.", 5)) {
		pea->name = talloc_strdup(mem_ctx, &ea_name[5]);
	} else {
		pea->name = talloc_strdup(mem_ctx, ea_name);
	}
	if (pea->name == NULL) {
		TALLOC_FREE(val);
		return NT_STATUS_NO_MEMORY;
	}
	pea->value.data = (unsigned char *)val;
	pea->value.length = (size_t)sizeret;
	return NT_STATUS_OK;
}

NTSTATUS get_ea_names_from_file(TALLOC_CTX *mem_ctx, connection_struct *conn,
				files_struct *fsp, const char *fname,
				char ***pnames, size_t *pnum_names)
{
	/* Get a list of all xattrs. Max namesize is 64k. */
	size_t ea_namelist_size = 1024;
	char *ea_namelist = NULL;

	char *p;
	char **names, **tmp;
	size_t num_names;
	ssize_t sizeret = -1;

	if (!lp_ea_support(SNUM(conn))) {
		*pnames = NULL;
		*pnum_names = 0;
		return NT_STATUS_OK;
	}

	/*
	 * TALLOC the result early to get the talloc hierarchy right.
	 */

	names = TALLOC_ARRAY(mem_ctx, char *, 1);
	if (names == NULL) {
		DEBUG(0, ("talloc failed\n"));
		return NT_STATUS_NO_MEMORY;
	}

	while (ea_namelist_size <= 65536) {

		ea_namelist = TALLOC_REALLOC_ARRAY(
			names, ea_namelist, char, ea_namelist_size);
		if (ea_namelist == NULL) {
			DEBUG(0, ("talloc failed\n"));
			TALLOC_FREE(names);
			return NT_STATUS_NO_MEMORY;
		}

		if (fsp && fsp->fh->fd != -1) {
			sizeret = SMB_VFS_FLISTXATTR(fsp, ea_namelist,
						     ea_namelist_size);
		} else {
			sizeret = SMB_VFS_LISTXATTR(conn, fname, ea_namelist,
						    ea_namelist_size);
		}

		if ((sizeret == -1) && (errno == ERANGE)) {
			ea_namelist_size *= 2;
		}
		else {
			break;
		}
	}

	if (sizeret == -1) {
		TALLOC_FREE(names);
		return map_nt_error_from_unix(errno);
	}

	DEBUG(10, ("get_ea_list_from_file: ea_namelist size = %u\n",
		   (unsigned int)sizeret));

	if (sizeret == 0) {
		TALLOC_FREE(names);
		*pnames = NULL;
		*pnum_names = 0;
		return NT_STATUS_OK;
	}

	/*
	 * Ensure the result is 0-terminated
	 */

	if (ea_namelist[sizeret-1] != '\0') {
		TALLOC_FREE(names);
		return NT_STATUS_INTERNAL_ERROR;
	}

	/*
	 * count the names
	 */
	num_names = 0;

	for (p = ea_namelist; p - ea_namelist < sizeret; p += strlen(p)+1) {
		num_names += 1;
	}

	tmp = TALLOC_REALLOC_ARRAY(mem_ctx, names, char *, num_names);
	if (tmp == NULL) {
		DEBUG(0, ("talloc failed\n"));
		TALLOC_FREE(names);
		return NT_STATUS_NO_MEMORY;
	}

	names = tmp;
	num_names = 0;

	for (p = ea_namelist; p - ea_namelist < sizeret; p += strlen(p)+1) {
		names[num_names++] = p;
	}

	*pnames = names;
	*pnum_names = num_names;
	return NT_STATUS_OK;
}

/****************************************************************************
 Return a linked list of the total EA's. Plus the total size
****************************************************************************/

static struct ea_list *get_ea_list_from_file(TALLOC_CTX *mem_ctx, connection_struct *conn, files_struct *fsp,
					const char *fname, size_t *pea_total_len)
{
	/* Get a list of all xattrs. Max namesize is 64k. */
	size_t i, num_names;
	char **names;
	struct ea_list *ea_list_head = NULL;
	NTSTATUS status;

	*pea_total_len = 0;

	if (!lp_ea_support(SNUM(conn))) {
		return NULL;
	}

	status = get_ea_names_from_file(talloc_tos(), conn, fsp, fname,
					&names, &num_names);

	if (!NT_STATUS_IS_OK(status) || (num_names == 0)) {
		return NULL;
	}

	for (i=0; i<num_names; i++) {
		struct ea_list *listp;
		fstring dos_ea_name;

		if (strnequal(names[i], "system.", 7)
		    || samba_private_attr_name(names[i]))
			continue;

		listp = TALLOC_P(mem_ctx, struct ea_list);
		if (listp == NULL) {
			return NULL;
		}

		if (!NT_STATUS_IS_OK(get_ea_value(mem_ctx, conn, fsp,
						  fname, names[i],
						  &listp->ea))) {
			return NULL;
		}

		push_ascii_fstring(dos_ea_name, listp->ea.name);

		*pea_total_len +=
			4 + strlen(dos_ea_name) + 1 + listp->ea.value.length;

		DEBUG(10,("get_ea_list_from_file: total_len = %u, %s, val len "
			  "= %u\n", (unsigned int)*pea_total_len, dos_ea_name,
			  (unsigned int)listp->ea.value.length));

		DLIST_ADD_END(ea_list_head, listp, struct ea_list *);

	}

	/* Add on 4 for total length. */
	if (*pea_total_len) {
		*pea_total_len += 4;
	}

	DEBUG(10, ("get_ea_list_from_file: total_len = %u\n",
		   (unsigned int)*pea_total_len));

	return ea_list_head;
}

/****************************************************************************
 Fill a qfilepathinfo buffer with EA's. Returns the length of the buffer
 that was filled.
****************************************************************************/

static unsigned int fill_ea_buffer(TALLOC_CTX *mem_ctx, char *pdata, unsigned int total_data_size,
	connection_struct *conn, struct ea_list *ea_list)
{
	unsigned int ret_data_size = 4;
	char *p = pdata;

	SMB_ASSERT(total_data_size >= 4);

	if (!lp_ea_support(SNUM(conn))) {
		SIVAL(pdata,4,0);
		return 4;
	}

	for (p = pdata + 4; ea_list; ea_list = ea_list->next) {
		size_t dos_namelen;
		fstring dos_ea_name;
		push_ascii_fstring(dos_ea_name, ea_list->ea.name);
		dos_namelen = strlen(dos_ea_name);
		if (dos_namelen > 255 || dos_namelen == 0) {
			break;
		}
		if (ea_list->ea.value.length > 65535) {
			break;
		}
		if (4 + dos_namelen + 1 + ea_list->ea.value.length > total_data_size) {
			break;
		}

		/* We know we have room. */
		SCVAL(p,0,ea_list->ea.flags);
		SCVAL(p,1,dos_namelen);
		SSVAL(p,2,ea_list->ea.value.length);
		fstrcpy(p+4, dos_ea_name);
		memcpy( p + 4 + dos_namelen + 1, ea_list->ea.value.data, ea_list->ea.value.length);

		total_data_size -= 4 + dos_namelen + 1 + ea_list->ea.value.length;
		p += 4 + dos_namelen + 1 + ea_list->ea.value.length;
	}

	ret_data_size = PTR_DIFF(p, pdata);
	DEBUG(10,("fill_ea_buffer: data_size = %u\n", ret_data_size ));
	SIVAL(pdata,0,ret_data_size);
	return ret_data_size;
}

static unsigned int estimate_ea_size(connection_struct *conn, files_struct *fsp, const char *fname)
{
	size_t total_ea_len = 0;
	TALLOC_CTX *mem_ctx = NULL;

	if (!lp_ea_support(SNUM(conn))) {
		return 0;
	}
	mem_ctx = talloc_tos();
	(void)get_ea_list_from_file(mem_ctx, conn, fsp, fname, &total_ea_len);
	return total_ea_len;
}

/****************************************************************************
 Ensure the EA name is case insensitive by matching any existing EA name.
****************************************************************************/

static void canonicalize_ea_name(connection_struct *conn, files_struct *fsp, const char *fname, fstring unix_ea_name)
{
	size_t total_ea_len;
	TALLOC_CTX *mem_ctx = talloc_tos();
	struct ea_list *ea_list = get_ea_list_from_file(mem_ctx, conn, fsp, fname, &total_ea_len);

	for (; ea_list; ea_list = ea_list->next) {
		if (strequal(&unix_ea_name[5], ea_list->ea.name)) {
			DEBUG(10,("canonicalize_ea_name: %s -> %s\n",
				&unix_ea_name[5], ea_list->ea.name));
			safe_strcpy(&unix_ea_name[5], ea_list->ea.name, sizeof(fstring)-6);
			break;
		}
	}
}

/****************************************************************************
 Set or delete an extended attribute.
****************************************************************************/

NTSTATUS set_ea(connection_struct *conn, files_struct *fsp, const char *fname, struct ea_list *ea_list)
{
	if (!lp_ea_support(SNUM(conn))) {
		return NT_STATUS_EAS_NOT_SUPPORTED;
	}

	for (;ea_list; ea_list = ea_list->next) {
		int ret;
		fstring unix_ea_name;

		fstrcpy(unix_ea_name, "user."); /* All EA's must start with user. */
		fstrcat(unix_ea_name, ea_list->ea.name);

		canonicalize_ea_name(conn, fsp, fname, unix_ea_name);

		DEBUG(10,("set_ea: ea_name %s ealen = %u\n", unix_ea_name, (unsigned int)ea_list->ea.value.length));

		if (samba_private_attr_name(unix_ea_name)) {
			DEBUG(10,("set_ea: ea name %s is a private Samba name.\n", unix_ea_name));
			return NT_STATUS_ACCESS_DENIED;
		}

		if (ea_list->ea.value.length == 0) {
			/* Remove the attribute. */
			if (fsp && (fsp->fh->fd != -1)) {
				DEBUG(10,("set_ea: deleting ea name %s on file %s by file descriptor.\n",
					unix_ea_name, fsp->fsp_name));
				ret = SMB_VFS_FREMOVEXATTR(fsp, unix_ea_name);
			} else {
				DEBUG(10,("set_ea: deleting ea name %s on file %s.\n",
					unix_ea_name, fname));
				ret = SMB_VFS_REMOVEXATTR(conn, fname, unix_ea_name);
			}
#ifdef ENOATTR
			/* Removing a non existent attribute always succeeds. */
			if (ret == -1 && errno == ENOATTR) {
				DEBUG(10,("set_ea: deleting ea name %s didn't exist - succeeding by default.\n",
						unix_ea_name));
				ret = 0;
			}
#endif
		} else {
			if (fsp && (fsp->fh->fd != -1)) {
				DEBUG(10,("set_ea: setting ea name %s on file %s by file descriptor.\n",
					unix_ea_name, fsp->fsp_name));
				ret = SMB_VFS_FSETXATTR(fsp, unix_ea_name,
							ea_list->ea.value.data, ea_list->ea.value.length, 0);
			} else {
				DEBUG(10,("set_ea: setting ea name %s on file %s.\n",
					unix_ea_name, fname));
				ret = SMB_VFS_SETXATTR(conn, fname, unix_ea_name,
							ea_list->ea.value.data, ea_list->ea.value.length, 0);
			}
		}

		if (ret == -1) {
#ifdef ENOTSUP
			if (errno == ENOTSUP) {
				return NT_STATUS_EAS_NOT_SUPPORTED;
			}
#endif
			return map_nt_error_from_unix(errno);
		}

	}
	return NT_STATUS_OK;
}
/****************************************************************************
 Read a list of EA names from an incoming data buffer. Create an ea_list with them.
****************************************************************************/

static struct ea_list *read_ea_name_list(TALLOC_CTX *ctx, const char *pdata, size_t data_size)
{
	struct ea_list *ea_list_head = NULL;
	size_t converted_size, offset = 0;

	while (offset + 2 < data_size) {
		struct ea_list *eal = TALLOC_ZERO_P(ctx, struct ea_list);
		unsigned int namelen = CVAL(pdata,offset);

		offset++; /* Go past the namelen byte. */

		/* integer wrap paranioa. */
		if ((offset + namelen < offset) || (offset + namelen < namelen) ||
				(offset > data_size) || (namelen > data_size) ||
				(offset + namelen >= data_size)) {
			break;
		}
		/* Ensure the name is null terminated. */
		if (pdata[offset + namelen] != '\0') {
			return NULL;
		}
		if (!pull_ascii_talloc(ctx, &eal->ea.name, &pdata[offset],
				       &converted_size)) {
			DEBUG(0,("read_ea_name_list: pull_ascii_talloc "
				 "failed: %s", strerror(errno)));
		}
		if (!eal->ea.name) {
			return NULL;
		}

		offset += (namelen + 1); /* Go past the name + terminating zero. */
		DLIST_ADD_END(ea_list_head, eal, struct ea_list *);
		DEBUG(10,("read_ea_name_list: read ea name %s\n", eal->ea.name));
	}

	return ea_list_head;
}

/****************************************************************************
 Read one EA list entry from the buffer.
****************************************************************************/

struct ea_list *read_ea_list_entry(TALLOC_CTX *ctx, const char *pdata, size_t data_size, size_t *pbytes_used)
{
	struct ea_list *eal = TALLOC_ZERO_P(ctx, struct ea_list);
	uint16 val_len;
	unsigned int namelen;
	size_t converted_size;

	if (!eal) {
		return NULL;
	}

	if (data_size < 6) {
		return NULL;
	}

	eal->ea.flags = CVAL(pdata,0);
	namelen = CVAL(pdata,1);
	val_len = SVAL(pdata,2);

	if (4 + namelen + 1 + val_len > data_size) {
		return NULL;
	}

	/* Ensure the name is null terminated. */
	if (pdata[namelen + 4] != '\0') {
		return NULL;
	}
	if (!pull_ascii_talloc(ctx, &eal->ea.name, pdata + 4, &converted_size)) {
		DEBUG(0,("read_ea_list_entry: pull_ascii_talloc failed: %s",
			 strerror(errno)));
	}
	if (!eal->ea.name) {
		return NULL;
	}

	eal->ea.value = data_blob_talloc(eal, NULL, (size_t)val_len + 1);
	if (!eal->ea.value.data) {
		return NULL;
	}

	memcpy(eal->ea.value.data, pdata + 4 + namelen + 1, val_len);

	/* Ensure we're null terminated just in case we print the value. */
	eal->ea.value.data[val_len] = '\0';
	/* But don't count the null. */
	eal->ea.value.length--;

	if (pbytes_used) {
		*pbytes_used = 4 + namelen + 1 + val_len;
	}

	DEBUG(10,("read_ea_list_entry: read ea name %s\n", eal->ea.name));
	dump_data(10, eal->ea.value.data, eal->ea.value.length);

	return eal;
}

/****************************************************************************
 Read a list of EA names and data from an incoming data buffer. Create an ea_list with them.
****************************************************************************/

static struct ea_list *read_ea_list(TALLOC_CTX *ctx, const char *pdata, size_t data_size)
{
	struct ea_list *ea_list_head = NULL;
	size_t offset = 0;
	size_t bytes_used = 0;

	while (offset < data_size) {
		struct ea_list *eal = read_ea_list_entry(ctx, pdata + offset, data_size - offset, &bytes_used);

		if (!eal) {
			return NULL;
		}

		DLIST_ADD_END(ea_list_head, eal, struct ea_list *);
		offset += bytes_used;
	}

	return ea_list_head;
}

/****************************************************************************
 Count the total EA size needed.
****************************************************************************/

static size_t ea_list_size(struct ea_list *ealist)
{
	fstring dos_ea_name;
	struct ea_list *listp;
	size_t ret = 0;

	for (listp = ealist; listp; listp = listp->next) {
		push_ascii_fstring(dos_ea_name, listp->ea.name);
		ret += 4 + strlen(dos_ea_name) + 1 + listp->ea.value.length;
	}
	/* Add on 4 for total length. */
	if (ret) {
		ret += 4;
	}

	return ret;
}

/****************************************************************************
 Return a union of EA's from a file list and a list of names.
 The TALLOC context for the two lists *MUST* be identical as we steal
 memory from one list to add to another. JRA.
****************************************************************************/

static struct ea_list *ea_list_union(struct ea_list *name_list, struct ea_list *file_list, size_t *total_ea_len)
{
	struct ea_list *nlistp, *flistp;

	for (nlistp = name_list; nlistp; nlistp = nlistp->next) {
		for (flistp = file_list; flistp; flistp = flistp->next) {
			if (strequal(nlistp->ea.name, flistp->ea.name)) {
				break;
			}
		}

		if (flistp) {
			/* Copy the data from this entry. */
			nlistp->ea.flags = flistp->ea.flags;
			nlistp->ea.value = flistp->ea.value;
		} else {
			/* Null entry. */
			nlistp->ea.flags = 0;
			ZERO_STRUCT(nlistp->ea.value);
		}
	}

	*total_ea_len = ea_list_size(name_list);
	return name_list;
}

/****************************************************************************
  Send the required number of replies back.
  We assume all fields other than the data fields are
  set correctly for the type of call.
  HACK ! Always assumes smb_setup field is zero.
****************************************************************************/

void send_trans2_replies(connection_struct *conn,
			struct smb_request *req,
			 const char *params,
			 int paramsize,
			 const char *pdata,
			 int datasize,
			 int max_data_bytes)
{
	/* As we are using a protocol > LANMAN1 then the max_send
	 variable must have been set in the sessetupX call.
	 This takes precedence over the max_xmit field in the
	 global struct. These different max_xmit variables should
	 be merged as this is now too confusing */

	int data_to_send = datasize;
	int params_to_send = paramsize;
	int useable_space;
	const char *pp = params;
	const char *pd = pdata;
	int params_sent_thistime, data_sent_thistime, total_sent_thistime;
	int alignment_offset = 1; /* JRA. This used to be 3. Set to 1 to make netmon parse ok. */
	int data_alignment_offset = 0;
	bool overflow = False;

	/* Modify the data_to_send and datasize and set the error if
	   we're trying to send more than max_data_bytes. We still send
	   the part of the packet(s) that fit. Strange, but needed
	   for OS/2. */

	if (max_data_bytes > 0 && datasize > max_data_bytes) {
		DEBUG(5,("send_trans2_replies: max_data_bytes %d exceeded by data %d\n",
			max_data_bytes, datasize ));
		datasize = data_to_send = max_data_bytes;
		overflow = True;
	}

	/* If there genuinely are no parameters or data to send just send the empty packet */

	if(params_to_send == 0 && data_to_send == 0) {
		reply_outbuf(req, 10, 0);
		show_msg((char *)req->outbuf);
		return;
	}

	/* When sending params and data ensure that both are nicely aligned */
	/* Only do this alignment when there is also data to send - else
		can cause NT redirector problems. */

	if (((params_to_send % 4) != 0) && (data_to_send != 0))
		data_alignment_offset = 4 - (params_to_send % 4);

	/* Space is bufsize minus Netbios over TCP header minus SMB header */
	/* The alignment_offset is to align the param bytes on an even byte
		boundary. NT 4.0 Beta needs this to work correctly. */

	useable_space = max_send - (smb_size
				    + 2 * 10 /* wct */
				    + alignment_offset
				    + data_alignment_offset);

	if (useable_space < 0) {
		DEBUG(0, ("send_trans2_replies failed sanity useable_space "
			  "= %d!!!", useable_space));
		exit_server_cleanly("send_trans2_replies: Not enough space");
	}

	while (params_to_send || data_to_send) {
		/* Calculate whether we will totally or partially fill this packet */

		total_sent_thistime = params_to_send + data_to_send;

		/* We can never send more than useable_space */
		/*
		 * Note that 'useable_space' does not include the alignment offsets,
		 * but we must include the alignment offsets in the calculation of
		 * the length of the data we send over the wire, as the alignment offsets
		 * are sent here. Fix from Marc_Jacobsen@hp.com.
		 */

		total_sent_thistime = MIN(total_sent_thistime, useable_space);

		reply_outbuf(req, 10, total_sent_thistime + alignment_offset
			     + data_alignment_offset);

		/*
		 * We might have SMBtrans2s in req which was transferred to
		 * the outbuf, fix that.
		 */
		SCVAL(req->outbuf, smb_com, SMBtrans2);

		/* Set total params and data to be sent */
		SSVAL(req->outbuf,smb_tprcnt,paramsize);
		SSVAL(req->outbuf,smb_tdrcnt,datasize);

		/* Calculate how many parameters and data we can fit into
		 * this packet. Parameters get precedence
		 */

		params_sent_thistime = MIN(params_to_send,useable_space);
		data_sent_thistime = useable_space - params_sent_thistime;
		data_sent_thistime = MIN(data_sent_thistime,data_to_send);

		SSVAL(req->outbuf,smb_prcnt, params_sent_thistime);

		/* smb_proff is the offset from the start of the SMB header to the
			parameter bytes, however the first 4 bytes of outbuf are
			the Netbios over TCP header. Thus use smb_base() to subtract
			them from the calculation */

		SSVAL(req->outbuf,smb_proff,
		      ((smb_buf(req->outbuf)+alignment_offset)
		       - smb_base(req->outbuf)));

		if(params_sent_thistime == 0)
			SSVAL(req->outbuf,smb_prdisp,0);
		else
			/* Absolute displacement of param bytes sent in this packet */
			SSVAL(req->outbuf,smb_prdisp,pp - params);

		SSVAL(req->outbuf,smb_drcnt, data_sent_thistime);
		if(data_sent_thistime == 0) {
			SSVAL(req->outbuf,smb_droff,0);
			SSVAL(req->outbuf,smb_drdisp, 0);
		} else {
			/* The offset of the data bytes is the offset of the
				parameter bytes plus the number of parameters being sent this time */
			SSVAL(req->outbuf, smb_droff,
			      ((smb_buf(req->outbuf)+alignment_offset)
			       - smb_base(req->outbuf))
			      + params_sent_thistime + data_alignment_offset);
			SSVAL(req->outbuf,smb_drdisp, pd - pdata);
		}

		/* Initialize the padding for alignment */

		if (alignment_offset != 0) {
			memset(smb_buf(req->outbuf), 0, alignment_offset);
		}

		/* Copy the param bytes into the packet */

		if(params_sent_thistime) {
			memcpy((smb_buf(req->outbuf)+alignment_offset), pp,
			       params_sent_thistime);
		}

		/* Copy in the data bytes */
		if(data_sent_thistime) {
			if (data_alignment_offset != 0) {
				memset((smb_buf(req->outbuf)+alignment_offset+
					params_sent_thistime), 0,
				       data_alignment_offset);
			}
			memcpy(smb_buf(req->outbuf)+alignment_offset
			       +params_sent_thistime+data_alignment_offset,
			       pd,data_sent_thistime);
		}

		DEBUG(9,("t2_rep: params_sent_thistime = %d, data_sent_thistime = %d, useable_space = %d\n",
			params_sent_thistime, data_sent_thistime, useable_space));
		DEBUG(9,("t2_rep: params_to_send = %d, data_to_send = %d, paramsize = %d, datasize = %d\n",
			params_to_send, data_to_send, paramsize, datasize));

		if (overflow) {
			error_packet_set((char *)req->outbuf,
					 ERRDOS,ERRbufferoverflow,
					 STATUS_BUFFER_OVERFLOW,
					 __LINE__,__FILE__);
		}

		/* Send the packet */
		show_msg((char *)req->outbuf);
		if (!srv_send_smb(smbd_server_fd(),
				(char *)req->outbuf,
				IS_CONN_ENCRYPTED(conn),
				&req->pcd))
			exit_server_cleanly("send_trans2_replies: srv_send_smb failed.");

		TALLOC_FREE(req->outbuf);

		pp += params_sent_thistime;
		pd += data_sent_thistime;

		params_to_send -= params_sent_thistime;
		data_to_send -= data_sent_thistime;

		/* Sanity check */
		if(params_to_send < 0 || data_to_send < 0) {
			DEBUG(0,("send_trans2_replies failed sanity check pts = %d, dts = %d\n!!!",
				params_to_send, data_to_send));
			return;
		}
	}

	return;
}

/****************************************************************************
 Reply to a TRANSACT2_OPEN.
****************************************************************************/

static void call_trans2open(connection_struct *conn,
			    struct smb_request *req,
			    char **pparams, int total_params,
			    char **ppdata, int total_data,
			    unsigned int max_data_bytes)
{
	char *params = *pparams;
	char *pdata = *ppdata;
	int deny_mode;
	int32 open_attr;
	bool oplock_request;
#if 0
	bool return_additional_info;
	int16 open_sattr;
	time_t open_time;
#endif
	int open_ofun;
	uint32 open_size;
	char *pname;
	char *fname = NULL;
	SMB_OFF_T size=0;
	int fattr=0,mtime=0;
	SMB_INO_T inode = 0;
	SMB_STRUCT_STAT sbuf;
	int smb_action = 0;
	files_struct *fsp;
	struct ea_list *ea_list = NULL;
	uint16 flags = 0;
	NTSTATUS status;
	uint32 access_mask;
	uint32 share_mode;
	uint32 create_disposition;
	uint32 create_options = 0;
	TALLOC_CTX *ctx = talloc_tos();

	SET_STAT_INVALID(sbuf);

	/*
	 * Ensure we have enough parameters to perform the operation.
	 */

	if (total_params < 29) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	flags = SVAL(params, 0);
	deny_mode = SVAL(params, 2);
	open_attr = SVAL(params,6);
        oplock_request = (flags & REQUEST_OPLOCK) ? EXCLUSIVE_OPLOCK : 0;
        if (oplock_request) {
                oplock_request |= (flags & REQUEST_BATCH_OPLOCK) ? BATCH_OPLOCK : 0;
        }

#if 0
	return_additional_info = BITSETW(params,0);
	open_sattr = SVAL(params, 4);
	open_time = make_unix_date3(params+8);
#endif
	open_ofun = SVAL(params,12);
	open_size = IVAL(params,14);
	pname = &params[28];

	if (IS_IPC(conn)) {
		reply_doserror(req, ERRSRV, ERRaccess);
		return;
	}

	srvstr_get_path(ctx, params, req->flags2, &fname, pname,
			total_params - 28, STR_TERMINATE,
			&status);
	if (!NT_STATUS_IS_OK(status)) {
		reply_nterror(req, status);
		return;
	}

	DEBUG(3,("call_trans2open %s deny_mode=0x%x attr=%d ofun=0x%x size=%d\n",
		fname, (unsigned int)deny_mode, (unsigned int)open_attr,
		(unsigned int)open_ofun, open_size));

	if (open_ofun == 0) {
		reply_nterror(req, NT_STATUS_OBJECT_NAME_COLLISION);
		return;
	}

	if (!map_open_params_to_ntcreate(fname, deny_mode, open_ofun,
				&access_mask,
				&share_mode,
				&create_disposition,
				&create_options)) {
		reply_doserror(req, ERRDOS, ERRbadaccess);
		return;
	}

	/* Any data in this call is an EA list. */
	if (total_data && (total_data != 4) && !lp_ea_support(SNUM(conn))) {
		reply_nterror(req, NT_STATUS_EAS_NOT_SUPPORTED);
		return;
	}

	if (total_data != 4) {
		if (total_data < 10) {
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}

		if (IVAL(pdata,0) > total_data) {
			DEBUG(10,("call_trans2open: bad total data size (%u) > %u\n",
				IVAL(pdata,0), (unsigned int)total_data));
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}

		ea_list = read_ea_list(talloc_tos(), pdata + 4,
				       total_data - 4);
		if (!ea_list) {
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}
	} else if (IVAL(pdata,0) != 4) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	status = SMB_VFS_CREATE_FILE(
		conn,					/* conn */
		req,					/* req */
		0,					/* root_dir_fid */
		fname,					/* fname */
		CFF_DOS_PATH,				/* create_file_flags */
		access_mask,				/* access_mask */
		share_mode,				/* share_access */
		create_disposition,			/* create_disposition*/
		create_options,				/* create_options */
		open_attr,				/* file_attributes */
		oplock_request,				/* oplock_request */
		open_size,				/* allocation_size */
		NULL,					/* sd */
		ea_list,				/* ea_list */
		&fsp,					/* result */
		&smb_action,				/* pinfo */
		&sbuf);					/* psbuf */

	if (!NT_STATUS_IS_OK(status)) {
		if (open_was_deferred(req->mid)) {
			/* We have re-scheduled this call. */
			return;
		}
		reply_openerror(req, status);
		return;
	}

	size = get_file_size_stat(&sbuf);
	fattr = dos_mode(conn,fsp->fsp_name,&sbuf);
	mtime = sbuf.st_mtime;
	inode = sbuf.st_ino;
	if (fattr & aDIR) {
		close_file(req, fsp, ERROR_CLOSE);
		reply_doserror(req, ERRDOS,ERRnoaccess);
		return;
	}

	/* Realloc the size of parameters and data we will return */
	*pparams = (char *)SMB_REALLOC(*pparams, 30);
	if(*pparams == NULL ) {
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}
	params = *pparams;

	SSVAL(params,0,fsp->fnum);
	SSVAL(params,2,fattr);
	srv_put_dos_date2(params,4, mtime);
	SIVAL(params,8, (uint32)size);
	SSVAL(params,12,deny_mode);
	SSVAL(params,14,0); /* open_type - file or directory. */
	SSVAL(params,16,0); /* open_state - only valid for IPC device. */

	if (oplock_request && lp_fake_oplocks(SNUM(conn))) {
		smb_action |= EXTENDED_OPLOCK_GRANTED;
	}

	SSVAL(params,18,smb_action);

	/*
	 * WARNING - this may need to be changed if SMB_INO_T <> 4 bytes.
	 */
	SIVAL(params,20,inode);
	SSVAL(params,24,0); /* Padding. */
	if (flags & 8) {
		uint32 ea_size = estimate_ea_size(conn, fsp, fsp->fsp_name);
		SIVAL(params, 26, ea_size);
	} else {
		SIVAL(params, 26, 0);
	}

	/* Send the required number of replies */
	send_trans2_replies(conn, req, params, 30, *ppdata, 0, max_data_bytes);
}

/*********************************************************
 Routine to check if a given string matches exactly.
 as a special case a mask of "." does NOT match. That
 is required for correct wildcard semantics
 Case can be significant or not.
**********************************************************/

static bool exact_match(connection_struct *conn,
		const char *str,
		const char *mask)
{
	if (mask[0] == '.' && mask[1] == 0)
		return False;
	if (dptr_has_wild(conn->dirptr)) {
		return False;
	}
	if (conn->case_sensitive)
		return strcmp(str,mask)==0;
	else
		return StrCaseCmp(str,mask) == 0;
}

/****************************************************************************
 Return the filetype for UNIX extensions.
****************************************************************************/

static uint32 unix_filetype(mode_t mode)
{
	if(S_ISREG(mode))
		return UNIX_TYPE_FILE;
	else if(S_ISDIR(mode))
		return UNIX_TYPE_DIR;
#ifdef S_ISLNK
	else if(S_ISLNK(mode))
		return UNIX_TYPE_SYMLINK;
#endif
#ifdef S_ISCHR
	else if(S_ISCHR(mode))
		return UNIX_TYPE_CHARDEV;
#endif
#ifdef S_ISBLK
	else if(S_ISBLK(mode))
		return UNIX_TYPE_BLKDEV;
#endif
#ifdef S_ISFIFO
	else if(S_ISFIFO(mode))
		return UNIX_TYPE_FIFO;
#endif
#ifdef S_ISSOCK
	else if(S_ISSOCK(mode))
		return UNIX_TYPE_SOCKET;
#endif

	DEBUG(0,("unix_filetype: unknown filetype %u\n", (unsigned)mode));
	return UNIX_TYPE_UNKNOWN;
}

/****************************************************************************
 Map wire perms onto standard UNIX permissions. Obey share restrictions.
****************************************************************************/

enum perm_type { PERM_NEW_FILE, PERM_NEW_DIR, PERM_EXISTING_FILE, PERM_EXISTING_DIR};

static NTSTATUS unix_perms_from_wire( connection_struct *conn,
				SMB_STRUCT_STAT *psbuf,
				uint32 perms,
				enum perm_type ptype,
				mode_t *ret_perms)
{
	mode_t ret = 0;

	if (perms == SMB_MODE_NO_CHANGE) {
		if (!VALID_STAT(*psbuf)) {
			return NT_STATUS_INVALID_PARAMETER;
		} else {
			*ret_perms = psbuf->st_mode;
			return NT_STATUS_OK;
		}
	}

	ret |= ((perms & UNIX_X_OTH ) ? S_IXOTH : 0);
	ret |= ((perms & UNIX_W_OTH ) ? S_IWOTH : 0);
	ret |= ((perms & UNIX_R_OTH ) ? S_IROTH : 0);
	ret |= ((perms & UNIX_X_GRP ) ? S_IXGRP : 0);
	ret |= ((perms & UNIX_W_GRP ) ? S_IWGRP : 0);
	ret |= ((perms & UNIX_R_GRP ) ? S_IRGRP : 0);
	ret |= ((perms & UNIX_X_USR ) ? S_IXUSR : 0);
	ret |= ((perms & UNIX_W_USR ) ? S_IWUSR : 0);
	ret |= ((perms & UNIX_R_USR ) ? S_IRUSR : 0);
#ifdef S_ISVTX
	ret |= ((perms & UNIX_STICKY ) ? S_ISVTX : 0);
#endif
#ifdef S_ISGID
	ret |= ((perms & UNIX_SET_GID ) ? S_ISGID : 0);
#endif
#ifdef S_ISUID
	ret |= ((perms & UNIX_SET_UID ) ? S_ISUID : 0);
#endif

	switch (ptype) {
	case PERM_NEW_FILE:
		/* Apply mode mask */
		ret &= lp_create_mask(SNUM(conn));
		/* Add in force bits */
		ret |= lp_force_create_mode(SNUM(conn));
		break;
	case PERM_NEW_DIR:
		ret &= lp_dir_mask(SNUM(conn));
		/* Add in force bits */
		ret |= lp_force_dir_mode(SNUM(conn));
		break;
	case PERM_EXISTING_FILE:
		/* Apply mode mask */
		ret &= lp_security_mask(SNUM(conn));
		/* Add in force bits */
		ret |= lp_force_security_mode(SNUM(conn));
		break;
	case PERM_EXISTING_DIR:
		/* Apply mode mask */
		ret &= lp_dir_security_mask(SNUM(conn));
		/* Add in force bits */
		ret |= lp_force_dir_security_mode(SNUM(conn));
		break;
	}

	*ret_perms = ret;
	return NT_STATUS_OK;
}

/****************************************************************************
 Needed to show the msdfs symlinks as directories. Modifies psbuf
 to be a directory if it's a msdfs link.
****************************************************************************/

static bool check_msdfs_link(connection_struct *conn,
				const char *pathname,
				SMB_STRUCT_STAT *psbuf)
{
	int saved_errno = errno;
	if(lp_host_msdfs() &&
		lp_msdfs_root(SNUM(conn)) &&
		is_msdfs_link(conn, pathname, psbuf)) {

		DEBUG(5,("check_msdfs_link: Masquerading msdfs link %s "
			"as a directory\n",
			pathname));
		psbuf->st_mode = (psbuf->st_mode & 0xFFF) | S_IFDIR;
		errno = saved_errno;
		return true;
	}
	errno = saved_errno;
	return false;
}


/****************************************************************************
 Get a level dependent lanman2 dir entry.
****************************************************************************/

static bool get_lanman2_dir_entry(TALLOC_CTX *ctx,
				connection_struct *conn,
				uint16 flags2,
				const char *path_mask,
				uint32 dirtype,
				int info_level,
				int requires_resume_key,
				bool dont_descend,
				bool ask_sharemode,
				char **ppdata,
				char *base_data,
				char *end_data,
				int space_remaining,
				bool *out_of_space,
				bool *got_exact_match,
				int *last_entry_off,
				struct ea_list *name_list)
{
	char *dname;
	bool found = False;
	SMB_STRUCT_STAT sbuf;
	const char *mask = NULL;
	char *pathreal = NULL;
	char *fname = NULL;
	char *p, *q, *pdata = *ppdata;
	uint32 reskey=0;
	long prev_dirpos=0;
	uint32 mode=0;
	SMB_OFF_T file_size = 0;
	uint64_t allocation_size = 0;
	uint32 len;
	struct timespec mdate_ts, adate_ts, create_date_ts;
	time_t mdate = (time_t)0, adate = (time_t)0, create_date = (time_t)0;
	char *nameptr;
	char *last_entry_ptr;
	bool was_8_3;
	uint32 nt_extmode; /* Used for NT connections instead of mode */
	bool needslash = ( conn->dirpath[strlen(conn->dirpath) -1] != '/');
	bool check_mangled_names = lp_manglednames(conn->params);
	char mangled_name[13]; /* mangled 8.3 name. */

	*out_of_space = False;
	*got_exact_match = False;

	ZERO_STRUCT(mdate_ts);
	ZERO_STRUCT(adate_ts);
	ZERO_STRUCT(create_date_ts);

	if (!conn->dirptr) {
		return(False);
	}

	p = strrchr_m(path_mask,'/');
	if(p != NULL) {
		if(p[1] == '\0') {
			mask = talloc_strdup(ctx,"*.*");
		} else {
			mask = p+1;
		}
	} else {
		mask = path_mask;
	}

	while (!found) {
		bool got_match;
		bool ms_dfs_link = False;

		/* Needed if we run out of space */
		long curr_dirpos = prev_dirpos = dptr_TellDir(conn->dirptr);
		dname = dptr_ReadDirName(ctx,conn->dirptr,&curr_dirpos,&sbuf);

		/*
		 * Due to bugs in NT client redirectors we are not using
		 * resume keys any more - set them to zero.
		 * Check out the related comments in findfirst/findnext.
		 * JRA.
		 */

		reskey = 0;

		DEBUG(8,("get_lanman2_dir_entry:readdir on dirptr 0x%lx now at offset %ld\n",
			(long)conn->dirptr,curr_dirpos));

		if (!dname) {
			return(False);
		}

		/*
		 * fname may get mangled, dname is never mangled.
		 * Whenever we're accessing the filesystem we use
		 * pathreal which is composed from dname.
		 */

		pathreal = NULL;
		fname = dname;

		/* Mangle fname if it's an illegal name. */
		if (mangle_must_mangle(dname,conn->params)) {
			if (!name_to_8_3(dname,mangled_name,True,conn->params)) {
				TALLOC_FREE(fname);
				continue; /* Error - couldn't mangle. */
			}
			fname = talloc_strdup(ctx, mangled_name);
			if (!fname) {
				return False;
			}
		}

		if(!(got_match = *got_exact_match = exact_match(conn, fname, mask))) {
			got_match = mask_match(fname, mask, conn->case_sensitive);
		}

		if(!got_match && check_mangled_names &&
		   !mangle_is_8_3(fname, False, conn->params)) {
			/*
			 * It turns out that NT matches wildcards against
			 * both long *and* short names. This may explain some
			 * of the wildcard wierdness from old DOS clients
			 * that some people have been seeing.... JRA.
			 */
			/* Force the mangling into 8.3. */
			if (!name_to_8_3( fname, mangled_name, False, conn->params)) {
				TALLOC_FREE(fname);
				continue; /* Error - couldn't mangle. */
			}

			if(!(got_match = *got_exact_match = exact_match(conn, mangled_name, mask))) {
				got_match = mask_match(mangled_name, mask, conn->case_sensitive);
			}
		}

		if (got_match) {
			bool isdots = (ISDOT(dname) || ISDOTDOT(dname));

			if (dont_descend && !isdots) {
				TALLOC_FREE(fname);
				continue;
			}

			if (needslash) {
				pathreal = NULL;
				pathreal = talloc_asprintf(ctx,
					"%s/%s",
					conn->dirpath,
					dname);
			} else {
				pathreal = talloc_asprintf(ctx,
					"%s%s",
					conn->dirpath,
					dname);
			}

			if (!pathreal) {
				TALLOC_FREE(fname);
				return False;
			}

			if (INFO_LEVEL_IS_UNIX(info_level)) {
				if (SMB_VFS_LSTAT(conn,pathreal,&sbuf) != 0) {
					DEBUG(5,("get_lanman2_dir_entry:Couldn't lstat [%s] (%s)\n",
						pathreal,strerror(errno)));
					TALLOC_FREE(pathreal);
					TALLOC_FREE(fname);
					continue;
				}
			} else if (!VALID_STAT(sbuf) && SMB_VFS_STAT(conn,pathreal,&sbuf) != 0) {
				/* Needed to show the msdfs symlinks as
				 * directories */

				ms_dfs_link = check_msdfs_link(conn, pathreal, &sbuf);
				if (!ms_dfs_link) {
					DEBUG(5,("get_lanman2_dir_entry:Couldn't stat [%s] (%s)\n",
						pathreal,strerror(errno)));
					TALLOC_FREE(pathreal);
					TALLOC_FREE(fname);
					continue;
				}
			}

			if (ms_dfs_link) {
				mode = dos_mode_msdfs(conn,pathreal,&sbuf);
			} else {
				mode = dos_mode(conn,pathreal,&sbuf);
			}

			if (!dir_check_ftype(conn,mode,dirtype)) {
				DEBUG(5,("get_lanman2_dir_entry: [%s] attribs didn't match %x\n",fname,dirtype));
				TALLOC_FREE(pathreal);
				TALLOC_FREE(fname);
				continue;
			}

			if (!(mode & aDIR)) {
				file_size = get_file_size_stat(&sbuf);
			}
			allocation_size = SMB_VFS_GET_ALLOC_SIZE(conn,NULL,&sbuf);

			mdate_ts = get_mtimespec(&sbuf);
			adate_ts = get_atimespec(&sbuf);
			create_date_ts = get_create_timespec(&sbuf,
			    lp_fake_dir_create_times(SNUM(conn)));

			if (ask_sharemode) {
				struct timespec write_time_ts;
				struct file_id fileid;

				fileid = vfs_file_id_from_sbuf(conn, &sbuf);
				get_file_infos(fileid, NULL, &write_time_ts);
				if (!null_timespec(write_time_ts)) {
					mdate_ts = write_time_ts;
				}
			}

			if (lp_dos_filetime_resolution(SNUM(conn))) {
				dos_filetime_timespec(&create_date_ts);
				dos_filetime_timespec(&mdate_ts);
				dos_filetime_timespec(&adate_ts);
			}

			create_date = convert_timespec_to_time_t(create_date_ts);
			mdate = convert_timespec_to_time_t(mdate_ts);
			adate = convert_timespec_to_time_t(adate_ts);

			DEBUG(5,("get_lanman2_dir_entry: found %s fname=%s\n",
				pathreal,fname));

			found = True;

			dptr_DirCacheAdd(conn->dirptr, dname, curr_dirpos);
		}

		if (!found)
			TALLOC_FREE(fname);
	}

	p = pdata;
	last_entry_ptr = p;

	nt_extmode = mode ? mode : FILE_ATTRIBUTE_NORMAL;

	switch (info_level) {
		case SMB_FIND_INFO_STANDARD:
			DEBUG(10,("get_lanman2_dir_entry: SMB_FIND_INFO_STANDARD\n"));
			if(requires_resume_key) {
				SIVAL(p,0,reskey);
				p += 4;
			}
			srv_put_dos_date2(p,0,create_date);
			srv_put_dos_date2(p,4,adate);
			srv_put_dos_date2(p,8,mdate);
			SIVAL(p,12,(uint32)file_size);
			SIVAL(p,16,(uint32)allocation_size);
			SSVAL(p,20,mode);
			p += 23;
			nameptr = p;
			if (flags2 & FLAGS2_UNICODE_STRINGS) {
				p += ucs2_align(base_data, p, 0);
			}
			len = srvstr_push(base_data, flags2, p,
					  fname, PTR_DIFF(end_data, p),
					  STR_TERMINATE);
			if (flags2 & FLAGS2_UNICODE_STRINGS) {
				if (len > 2) {
					SCVAL(nameptr, -1, len - 2);
				} else {
					SCVAL(nameptr, -1, 0);
				}
			} else {
				if (len > 1) {
					SCVAL(nameptr, -1, len - 1);
				} else {
					SCVAL(nameptr, -1, 0);
				}
			}
			p += len;
			break;

		case SMB_FIND_EA_SIZE:
			DEBUG(10,("get_lanman2_dir_entry: SMB_FIND_EA_SIZE\n"));
			if(requires_resume_key) {
				SIVAL(p,0,reskey);
				p += 4;
			}
			srv_put_dos_date2(p,0,create_date);
			srv_put_dos_date2(p,4,adate);
			srv_put_dos_date2(p,8,mdate);
			SIVAL(p,12,(uint32)file_size);
			SIVAL(p,16,(uint32)allocation_size);
			SSVAL(p,20,mode);
			{
				unsigned int ea_size = estimate_ea_size(conn, NULL, pathreal);
				SIVAL(p,22,ea_size); /* Extended attributes */
			}
			p += 27;
			nameptr = p - 1;
			len = srvstr_push(base_data, flags2,
					  p, fname, PTR_DIFF(end_data, p),
					  STR_TERMINATE | STR_NOALIGN);
			if (flags2 & FLAGS2_UNICODE_STRINGS) {
				if (len > 2) {
					len -= 2;
				} else {
					len = 0;
				}
			} else {
				if (len > 1) {
					len -= 1;
				} else {
					len = 0;
				}
			}
			SCVAL(nameptr,0,len);
			p += len;
			SCVAL(p,0,0); p += 1; /* Extra zero byte ? - why.. */
			break;

		case SMB_FIND_EA_LIST:
		{
			struct ea_list *file_list = NULL;
			size_t ea_len = 0;

			DEBUG(10,("get_lanman2_dir_entry: SMB_FIND_EA_LIST\n"));
			if (!name_list) {
				return False;
			}
			if(requires_resume_key) {
				SIVAL(p,0,reskey);
				p += 4;
			}
			srv_put_dos_date2(p,0,create_date);
			srv_put_dos_date2(p,4,adate);
			srv_put_dos_date2(p,8,mdate);
			SIVAL(p,12,(uint32)file_size);
			SIVAL(p,16,(uint32)allocation_size);
			SSVAL(p,20,mode);
			p += 22; /* p now points to the EA area. */

			file_list = get_ea_list_from_file(ctx, conn, NULL, pathreal, &ea_len);
			name_list = ea_list_union(name_list, file_list, &ea_len);

			/* We need to determine if this entry will fit in the space available. */
			/* Max string size is 255 bytes. */
			if (PTR_DIFF(p + 255 + ea_len,pdata) > space_remaining) {
				/* Move the dirptr back to prev_dirpos */
				dptr_SeekDir(conn->dirptr, prev_dirpos);
				*out_of_space = True;
				DEBUG(9,("get_lanman2_dir_entry: out of space\n"));
				return False; /* Not finished - just out of space */
			}

			/* Push the ea_data followed by the name. */
			p += fill_ea_buffer(ctx, p, space_remaining, conn, name_list);
			nameptr = p;
			len = srvstr_push(base_data, flags2,
					  p + 1, fname, PTR_DIFF(end_data, p+1),
					  STR_TERMINATE | STR_NOALIGN);
			if (flags2 & FLAGS2_UNICODE_STRINGS) {
				if (len > 2) {
					len -= 2;
				} else {
					len = 0;
				}
			} else {
				if (len > 1) {
					len -= 1;
				} else {
					len = 0;
				}
			}
			SCVAL(nameptr,0,len);
			p += len + 1;
			SCVAL(p,0,0); p += 1; /* Extra zero byte ? - why.. */
			break;
		}

		case SMB_FIND_FILE_BOTH_DIRECTORY_INFO:
			DEBUG(10,("get_lanman2_dir_entry: SMB_FIND_FILE_BOTH_DIRECTORY_INFO\n"));
			was_8_3 = mangle_is_8_3(fname, True, conn->params);
			p += 4;
			SIVAL(p,0,reskey); p += 4;
			put_long_date_timespec(p,create_date_ts); p += 8;
			put_long_date_timespec(p,adate_ts); p += 8;
			put_long_date_timespec(p,mdate_ts); p += 8;
			put_long_date_timespec(p,mdate_ts); p += 8;
			SOFF_T(p,0,file_size); p += 8;
			SOFF_T(p,0,allocation_size); p += 8;
			SIVAL(p,0,nt_extmode); p += 4;
			q = p; p += 4; /* q is placeholder for name length. */
			{
				unsigned int ea_size = estimate_ea_size(conn, NULL, pathreal);
				SIVAL(p,0,ea_size); /* Extended attributes */
				p += 4;
			}
			/* Clear the short name buffer. This is
			 * IMPORTANT as not doing so will trigger
			 * a Win2k client bug. JRA.
			 */
			if (!was_8_3 && check_mangled_names) {
				if (!name_to_8_3(fname,mangled_name,True,
						   conn->params)) {
					/* Error - mangle failed ! */
					memset(mangled_name,'\0',12);
				}
				mangled_name[12] = 0;
				len = srvstr_push(base_data, flags2,
						  p+2, mangled_name, 24,
						  STR_UPPER|STR_UNICODE);
				if (len < 24) {
					memset(p + 2 + len,'\0',24 - len);
				}
				SSVAL(p, 0, len);
			} else {
				memset(p,'\0',26);
			}
			p += 2 + 24;
			len = srvstr_push(base_data, flags2, p,
					  fname, PTR_DIFF(end_data, p),
					  STR_TERMINATE_ASCII);
			SIVAL(q,0,len);
			p += len;
			SIVAL(p,0,0); /* Ensure any padding is null. */
			len = PTR_DIFF(p, pdata);
			len = (len + 3) & ~3;
			SIVAL(pdata,0,len);
			p = pdata + len;
			break;

		case SMB_FIND_FILE_DIRECTORY_INFO:
			DEBUG(10,("get_lanman2_dir_entry: SMB_FIND_FILE_DIRECTORY_INFO\n"));
			p += 4;
			SIVAL(p,0,reskey); p += 4;
			put_long_date_timespec(p,create_date_ts); p += 8;
			put_long_date_timespec(p,adate_ts); p += 8;
			put_long_date_timespec(p,mdate_ts); p += 8;
			put_long_date_timespec(p,mdate_ts); p += 8;
			SOFF_T(p,0,file_size); p += 8;
			SOFF_T(p,0,allocation_size); p += 8;
			SIVAL(p,0,nt_extmode); p += 4;
			len = srvstr_push(base_data, flags2,
					  p + 4, fname, PTR_DIFF(end_data, p+4),
					  STR_TERMINATE_ASCII);
			SIVAL(p,0,len);
			p += 4 + len;
			SIVAL(p,0,0); /* Ensure any padding is null. */
			len = PTR_DIFF(p, pdata);
			len = (len + 3) & ~3;
			SIVAL(pdata,0,len);
			p = pdata + len;
			break;

		case SMB_FIND_FILE_FULL_DIRECTORY_INFO:
			DEBUG(10,("get_lanman2_dir_entry: SMB_FIND_FILE_FULL_DIRECTORY_INFO\n"));
			p += 4;
			SIVAL(p,0,reskey); p += 4;
			put_long_date_timespec(p,create_date_ts); p += 8;
			put_long_date_timespec(p,adate_ts); p += 8;
			put_long_date_timespec(p,mdate_ts); p += 8;
			put_long_date_timespec(p,mdate_ts); p += 8;
			SOFF_T(p,0,file_size); p += 8;
			SOFF_T(p,0,allocation_size); p += 8;
			SIVAL(p,0,nt_extmode); p += 4;
			q = p; p += 4; /* q is placeholder for name length. */
			{
				unsigned int ea_size = estimate_ea_size(conn, NULL, pathreal);
				SIVAL(p,0,ea_size); /* Extended attributes */
				p +=4;
			}
			len = srvstr_push(base_data, flags2, p,
					  fname, PTR_DIFF(end_data, p),
					  STR_TERMINATE_ASCII);
			SIVAL(q, 0, len);
			p += len;

			SIVAL(p,0,0); /* Ensure any padding is null. */
			len = PTR_DIFF(p, pdata);
			len = (len + 3) & ~3;
			SIVAL(pdata,0,len);
			p = pdata + len;
			break;

		case SMB_FIND_FILE_NAMES_INFO:
			DEBUG(10,("get_lanman2_dir_entry: SMB_FIND_FILE_NAMES_INFO\n"));
			p += 4;
			SIVAL(p,0,reskey); p += 4;
			p += 4;
			/* this must *not* be null terminated or w2k gets in a loop trying to set an
			   acl on a dir (tridge) */
			len = srvstr_push(base_data, flags2, p,
					  fname, PTR_DIFF(end_data, p),
					  STR_TERMINATE_ASCII);
			SIVAL(p, -4, len);
			p += len;
			SIVAL(p,0,0); /* Ensure any padding is null. */
			len = PTR_DIFF(p, pdata);
			len = (len + 3) & ~3;
			SIVAL(pdata,0,len);
			p = pdata + len;
			break;

		case SMB_FIND_ID_FULL_DIRECTORY_INFO:
			DEBUG(10,("get_lanman2_dir_entry: SMB_FIND_ID_FULL_DIRECTORY_INFO\n"));
			p += 4;
			SIVAL(p,0,reskey); p += 4;
			put_long_date_timespec(p,create_date_ts); p += 8;
			put_long_date_timespec(p,adate_ts); p += 8;
			put_long_date_timespec(p,mdate_ts); p += 8;
			put_long_date_timespec(p,mdate_ts); p += 8;
			SOFF_T(p,0,file_size); p += 8;
			SOFF_T(p,0,allocation_size); p += 8;
			SIVAL(p,0,nt_extmode); p += 4;
			q = p; p += 4; /* q is placeholder for name length. */
			{
				unsigned int ea_size = estimate_ea_size(conn, NULL, pathreal);
				SIVAL(p,0,ea_size); /* Extended attributes */
				p +=4;
			}
			SIVAL(p,0,0); p += 4; /* Unknown - reserved ? */
			SIVAL(p,0,sbuf.st_ino); p += 4; /* FileIndexLow */
			SIVAL(p,0,sbuf.st_dev); p += 4; /* FileIndexHigh */
			len = srvstr_push(base_data, flags2, p,
					  fname, PTR_DIFF(end_data, p),
					  STR_TERMINATE_ASCII);
			SIVAL(q, 0, len);
			p += len; 
			SIVAL(p,0,0); /* Ensure any padding is null. */
			len = PTR_DIFF(p, pdata);
			len = (len + 3) & ~3;
			SIVAL(pdata,0,len);
			p = pdata + len;
			break;

		case SMB_FIND_ID_BOTH_DIRECTORY_INFO:
			DEBUG(10,("get_lanman2_dir_entry: SMB_FIND_ID_BOTH_DIRECTORY_INFO\n"));
			was_8_3 = mangle_is_8_3(fname, True, conn->params);
			p += 4;
			SIVAL(p,0,reskey); p += 4;
			put_long_date_timespec(p,create_date_ts); p += 8;
			put_long_date_timespec(p,adate_ts); p += 8;
			put_long_date_timespec(p,mdate_ts); p += 8;
			put_long_date_timespec(p,mdate_ts); p += 8;
			SOFF_T(p,0,file_size); p += 8;
			SOFF_T(p,0,allocation_size); p += 8;
			SIVAL(p,0,nt_extmode); p += 4;
			q = p; p += 4; /* q is placeholder for name length */
			{
				unsigned int ea_size = estimate_ea_size(conn, NULL, pathreal);
				SIVAL(p,0,ea_size); /* Extended attributes */
				p +=4;
			}
			/* Clear the short name buffer. This is
			 * IMPORTANT as not doing so will trigger
			 * a Win2k client bug. JRA.
			 */
			if (!was_8_3 && check_mangled_names) {
				if (!name_to_8_3(fname,mangled_name,True,
						conn->params)) {
					/* Error - mangle failed ! */
					memset(mangled_name,'\0',12);
				}
				mangled_name[12] = 0;
				len = srvstr_push(base_data, flags2,
						  p+2, mangled_name, 24,
						  STR_UPPER|STR_UNICODE);
				SSVAL(p, 0, len);
				if (len < 24) {
					memset(p + 2 + len,'\0',24 - len);
				}
				SSVAL(p, 0, len);
			} else {
				memset(p,'\0',26);
			}
			p += 26;
			SSVAL(p,0,0); p += 2; /* Reserved ? */
			SIVAL(p,0,sbuf.st_ino); p += 4; /* FileIndexLow */
			SIVAL(p,0,sbuf.st_dev); p += 4; /* FileIndexHigh */
			len = srvstr_push(base_data, flags2, p,
					  fname, PTR_DIFF(end_data, p),
					  STR_TERMINATE_ASCII);
			SIVAL(q,0,len);
			p += len;
			SIVAL(p,0,0); /* Ensure any padding is null. */
			len = PTR_DIFF(p, pdata);
			len = (len + 3) & ~3;
			SIVAL(pdata,0,len);
			p = pdata + len;
			break;

		/* CIFS UNIX Extension. */

		case SMB_FIND_FILE_UNIX:
		case SMB_FIND_FILE_UNIX_INFO2:
			p+= 4;
			SIVAL(p,0,reskey); p+= 4;    /* Used for continuing search. */

			/* Begin of SMB_QUERY_FILE_UNIX_BASIC */

			if (info_level == SMB_FIND_FILE_UNIX) {
				DEBUG(10,("get_lanman2_dir_entry: SMB_FIND_FILE_UNIX\n"));
				p = store_file_unix_basic(conn, p,
							NULL, &sbuf);
				len = srvstr_push(base_data, flags2, p,
						  fname, PTR_DIFF(end_data, p),
						  STR_TERMINATE);
			} else {
				DEBUG(10,("get_lanman2_dir_entry: SMB_FIND_FILE_UNIX_INFO2\n"));
				p = store_file_unix_basic_info2(conn, p,
							NULL, &sbuf);
				nameptr = p;
				p += 4;
				len = srvstr_push(base_data, flags2, p, fname,
						  PTR_DIFF(end_data, p), 0);
				SIVAL(nameptr, 0, len);
			}

			p += len;
			SIVAL(p,0,0); /* Ensure any padding is null. */

			len = PTR_DIFF(p, pdata);
			len = (len + 3) & ~3;
			SIVAL(pdata,0,len);	/* Offset from this structure to the beginning of the next one */
			p = pdata + len;
			/* End of SMB_QUERY_FILE_UNIX_BASIC */

			break;

		default:
			TALLOC_FREE(fname);
			return(False);
	}

	TALLOC_FREE(fname);
	if (PTR_DIFF(p,pdata) > space_remaining) {
		/* Move the dirptr back to prev_dirpos */
		dptr_SeekDir(conn->dirptr, prev_dirpos);
		*out_of_space = True;
		DEBUG(9,("get_lanman2_dir_entry: out of space\n"));
		return False; /* Not finished - just out of space */
	}

	/* Setup the last entry pointer, as an offset from base_data */
	*last_entry_off = PTR_DIFF(last_entry_ptr,base_data);
	/* Advance the data pointer to the next slot */
	*ppdata = p;

	return(found);
}

/****************************************************************************
 Reply to a TRANS2_FINDFIRST.
****************************************************************************/

static void call_trans2findfirst(connection_struct *conn,
				 struct smb_request *req,
				 char **pparams, int total_params,
				 char **ppdata, int total_data,
				 unsigned int max_data_bytes)
{
	/* We must be careful here that we don't return more than the
		allowed number of data bytes. If this means returning fewer than
		maxentries then so be it. We assume that the redirector has
		enough room for the fixed number of parameter bytes it has
		requested. */
	char *params = *pparams;
	char *pdata = *ppdata;
	char *data_end;
	uint32 dirtype;
	int maxentries;
	uint16 findfirst_flags;
	bool close_after_first;
	bool close_if_end;
	bool requires_resume_key;
	int info_level;
	char *directory = NULL;
	char *mask = NULL;
	char *p;
	int last_entry_off=0;
	int dptr_num = -1;
	int numentries = 0;
	int i;
	bool finished = False;
	bool dont_descend = False;
	bool out_of_space = False;
	int space_remaining;
	bool mask_contains_wcard = False;
	SMB_STRUCT_STAT sbuf;
	struct ea_list *ea_list = NULL;
	NTSTATUS ntstatus = NT_STATUS_OK;
	bool ask_sharemode = lp_parm_bool(SNUM(conn), "smbd", "search ask sharemode", true);
	TALLOC_CTX *ctx = talloc_tos();

	if (total_params < 13) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	dirtype = SVAL(params,0);
	maxentries = SVAL(params,2);
	findfirst_flags = SVAL(params,4);
	close_after_first = (findfirst_flags & FLAG_TRANS2_FIND_CLOSE);
	close_if_end = (findfirst_flags & FLAG_TRANS2_FIND_CLOSE_IF_END);
	requires_resume_key = (findfirst_flags & FLAG_TRANS2_FIND_REQUIRE_RESUME);
	info_level = SVAL(params,6);

	DEBUG(3,("call_trans2findfirst: dirtype = %x, maxentries = %d, close_after_first=%d, \
close_if_end = %d requires_resume_key = %d level = 0x%x, max_data_bytes = %d\n",
		(unsigned int)dirtype, maxentries, close_after_first, close_if_end, requires_resume_key,
		info_level, max_data_bytes));

	if (!maxentries) {
		/* W2K3 seems to treat zero as 1. */
		maxentries = 1;
	}

	switch (info_level) {
		case SMB_FIND_INFO_STANDARD:
		case SMB_FIND_EA_SIZE:
		case SMB_FIND_EA_LIST:
		case SMB_FIND_FILE_DIRECTORY_INFO:
		case SMB_FIND_FILE_FULL_DIRECTORY_INFO:
		case SMB_FIND_FILE_NAMES_INFO:
		case SMB_FIND_FILE_BOTH_DIRECTORY_INFO:
		case SMB_FIND_ID_FULL_DIRECTORY_INFO:
		case SMB_FIND_ID_BOTH_DIRECTORY_INFO:
			break;
		case SMB_FIND_FILE_UNIX:
		case SMB_FIND_FILE_UNIX_INFO2:
			/* Always use filesystem for UNIX mtime query. */
			ask_sharemode = false;
			if (!lp_unix_extensions()) {
				reply_nterror(req, NT_STATUS_INVALID_LEVEL);
				return;
			}
			break;
		default:
			reply_nterror(req, NT_STATUS_INVALID_LEVEL);
			return;
	}

	srvstr_get_path_wcard(ctx, params, req->flags2, &directory,
			      params+12, total_params - 12,
			      STR_TERMINATE, &ntstatus, &mask_contains_wcard);
	if (!NT_STATUS_IS_OK(ntstatus)) {
		reply_nterror(req, ntstatus);
		return;
	}

	ntstatus = resolve_dfspath_wcard(ctx, conn,
			req->flags2 & FLAGS2_DFS_PATHNAMES,
			directory,
			&directory,
			&mask_contains_wcard);
	if (!NT_STATUS_IS_OK(ntstatus)) {
		if (NT_STATUS_EQUAL(ntstatus,NT_STATUS_PATH_NOT_COVERED)) {
			reply_botherror(req, NT_STATUS_PATH_NOT_COVERED,
					ERRSRV, ERRbadpath);
			return;
		}
		reply_nterror(req, ntstatus);
		return;
	}

	ntstatus = unix_convert(ctx, conn, directory, True, &directory, &mask, &sbuf);
	if (!NT_STATUS_IS_OK(ntstatus)) {
		reply_nterror(req, ntstatus);
		return;
	}

	ntstatus = check_name(conn, directory);
	if (!NT_STATUS_IS_OK(ntstatus)) {
		reply_nterror(req, ntstatus);
		return;
	}

	p = strrchr_m(directory,'/');
	if(p == NULL) {
		/* Windows and OS/2 systems treat search on the root '\' as if it were '\*' */
		if((directory[0] == '.') && (directory[1] == '\0')) {
			mask = talloc_strdup(ctx,"*");
			if (!mask) {
				reply_nterror(req, NT_STATUS_NO_MEMORY);
				return;
			}
			mask_contains_wcard = True;
		}
		directory = talloc_strdup(talloc_tos(), "./");
		if (!directory) {
			reply_nterror(req, NT_STATUS_NO_MEMORY);
			return;
		}
	} else {
		*p = 0;
	}

	DEBUG(5,("dir=%s, mask = %s\n",directory, mask));

	if (info_level == SMB_FIND_EA_LIST) {
		uint32 ea_size;

		if (total_data < 4) {
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}

		ea_size = IVAL(pdata,0);
		if (ea_size != total_data) {
			DEBUG(4,("call_trans2findfirst: Rejecting EA request with incorrect \
total_data=%u (should be %u)\n", (unsigned int)total_data, (unsigned int)IVAL(pdata,0) ));
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}

		if (!lp_ea_support(SNUM(conn))) {
			reply_doserror(req, ERRDOS, ERReasnotsupported);
			return;
		}

		/* Pull out the list of names. */
		ea_list = read_ea_name_list(ctx, pdata + 4, ea_size - 4);
		if (!ea_list) {
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}
	}

	*ppdata = (char *)SMB_REALLOC(
		*ppdata, max_data_bytes + DIR_ENTRY_SAFETY_MARGIN);
	if(*ppdata == NULL ) {
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}
	pdata = *ppdata;
	data_end = pdata + max_data_bytes + DIR_ENTRY_SAFETY_MARGIN - 1;

	/* Realloc the params space */
	*pparams = (char *)SMB_REALLOC(*pparams, 10);
	if (*pparams == NULL) {
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}
	params = *pparams;

	/* Save the wildcard match and attribs we are using on this directory -
		needed as lanman2 assumes these are being saved between calls */

	ntstatus = dptr_create(conn,
				directory,
				False,
				True,
				req->smbpid,
				mask,
				mask_contains_wcard,
				dirtype,
				&conn->dirptr);

	if (!NT_STATUS_IS_OK(ntstatus)) {
		reply_nterror(req, ntstatus);
		return;
	}

	dptr_num = dptr_dnum(conn->dirptr);
	DEBUG(4,("dptr_num is %d, wcard = %s, attr = %d\n", dptr_num, mask, dirtype));

	/* Initialize per TRANS2_FIND_FIRST operation data */
	dptr_init_search_op(conn->dirptr);

	/* We don't need to check for VOL here as this is returned by
		a different TRANS2 call. */

	DEBUG(8,("dirpath=<%s> dontdescend=<%s>\n", conn->dirpath,lp_dontdescend(SNUM(conn))));
	if (in_list(conn->dirpath,lp_dontdescend(SNUM(conn)),conn->case_sensitive))
		dont_descend = True;

	p = pdata;
	space_remaining = max_data_bytes;
	out_of_space = False;

	for (i=0;(i<maxentries) && !finished && !out_of_space;i++) {
		bool got_exact_match = False;

		/* this is a heuristic to avoid seeking the dirptr except when
			absolutely necessary. It allows for a filename of about 40 chars */
		if (space_remaining < DIRLEN_GUESS && numentries > 0) {
			out_of_space = True;
			finished = False;
		} else {
			finished = !get_lanman2_dir_entry(ctx,
					conn,
					req->flags2,
					mask,dirtype,info_level,
					requires_resume_key,dont_descend,
					ask_sharemode,
					&p,pdata,data_end,
					space_remaining, &out_of_space,
					&got_exact_match,
					&last_entry_off, ea_list);
		}

		if (finished && out_of_space)
			finished = False;

		if (!finished && !out_of_space)
			numentries++;

		/*
		 * As an optimisation if we know we aren't looking
		 * for a wildcard name (ie. the name matches the wildcard exactly)
		 * then we can finish on any (first) match.
		 * This speeds up large directory searches. JRA.
		 */

		if(got_exact_match)
			finished = True;

		/* Ensure space_remaining never goes -ve. */
		if (PTR_DIFF(p,pdata) > max_data_bytes) {
			space_remaining = 0;
			out_of_space = true;
		} else {
			space_remaining = max_data_bytes - PTR_DIFF(p,pdata);
		}
	}

	/* Check if we can close the dirptr */
	if(close_after_first || (finished && close_if_end)) {
		DEBUG(5,("call_trans2findfirst - (2) closing dptr_num %d\n", dptr_num));
		dptr_close(&dptr_num);
	}

	/*
	 * If there are no matching entries we must return ERRDOS/ERRbadfile -
	 * from observation of NT. NB. This changes to ERRDOS,ERRnofiles if
	 * the protocol level is less than NT1. Tested with smbclient. JRA.
	 * This should fix the OS/2 client bug #2335.
	 */

	if(numentries == 0) {
		dptr_close(&dptr_num);
		if (Protocol < PROTOCOL_NT1) {
			reply_doserror(req, ERRDOS, ERRnofiles);
			return;
		} else {
			reply_botherror(req, NT_STATUS_NO_SUCH_FILE,
					ERRDOS, ERRbadfile);
			return;
		}
	}

	/* At this point pdata points to numentries directory entries. */

	/* Set up the return parameter block */
	SSVAL(params,0,dptr_num);
	SSVAL(params,2,numentries);
	SSVAL(params,4,finished);
	SSVAL(params,6,0); /* Never an EA error */
	SSVAL(params,8,last_entry_off);

	send_trans2_replies(conn, req, params, 10, pdata, PTR_DIFF(p,pdata),
			    max_data_bytes);

	if ((! *directory) && dptr_path(dptr_num)) {
		directory = talloc_strdup(talloc_tos(),dptr_path(dptr_num));
		if (!directory) {
			reply_nterror(req, NT_STATUS_NO_MEMORY);
		}
	}

	DEBUG( 4, ( "%s mask=%s directory=%s dirtype=%d numentries=%d\n",
		smb_fn_name(req->cmd),
		mask, directory, dirtype, numentries ) );

	/*
	 * Force a name mangle here to ensure that the
	 * mask as an 8.3 name is top of the mangled cache.
	 * The reasons for this are subtle. Don't remove
	 * this code unless you know what you are doing
	 * (see PR#13758). JRA.
	 */

	if(!mangle_is_8_3_wildcards( mask, False, conn->params)) {
		char mangled_name[13];
		name_to_8_3(mask, mangled_name, True, conn->params);
	}

	return;
}

/****************************************************************************
 Reply to a TRANS2_FINDNEXT.
****************************************************************************/

static void call_trans2findnext(connection_struct *conn,
				struct smb_request *req,
				char **pparams, int total_params,
				char **ppdata, int total_data,
				unsigned int max_data_bytes)
{
	/* We must be careful here that we don't return more than the
		allowed number of data bytes. If this means returning fewer than
		maxentries then so be it. We assume that the redirector has
		enough room for the fixed number of parameter bytes it has
		requested. */
	char *params = *pparams;
	char *pdata = *ppdata;
	char *data_end;
	int dptr_num;
	int maxentries;
	uint16 info_level;
	uint32 resume_key;
	uint16 findnext_flags;
	bool close_after_request;
	bool close_if_end;
	bool requires_resume_key;
	bool continue_bit;
	bool mask_contains_wcard = False;
	char *resume_name = NULL;
	const char *mask = NULL;
	const char *directory = NULL;
	char *p = NULL;
	uint16 dirtype;
	int numentries = 0;
	int i, last_entry_off=0;
	bool finished = False;
	bool dont_descend = False;
	bool out_of_space = False;
	int space_remaining;
	struct ea_list *ea_list = NULL;
	NTSTATUS ntstatus = NT_STATUS_OK;
	bool ask_sharemode = lp_parm_bool(SNUM(conn), "smbd", "search ask sharemode", true);
	TALLOC_CTX *ctx = talloc_tos();

	if (total_params < 13) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	dptr_num = SVAL(params,0);
	maxentries = SVAL(params,2);
	info_level = SVAL(params,4);
	resume_key = IVAL(params,6);
	findnext_flags = SVAL(params,10);
	close_after_request = (findnext_flags & FLAG_TRANS2_FIND_CLOSE);
	close_if_end = (findnext_flags & FLAG_TRANS2_FIND_CLOSE_IF_END);
	requires_resume_key = (findnext_flags & FLAG_TRANS2_FIND_REQUIRE_RESUME);
	continue_bit = (findnext_flags & FLAG_TRANS2_FIND_CONTINUE);

	srvstr_get_path_wcard(ctx, params, req->flags2, &resume_name,
			      params+12,
			      total_params - 12, STR_TERMINATE, &ntstatus,
			      &mask_contains_wcard);
	if (!NT_STATUS_IS_OK(ntstatus)) {
		/* Win9x or OS/2 can send a resume name of ".." or ".". This will cause the parser to
		   complain (it thinks we're asking for the directory above the shared
		   path or an invalid name). Catch this as the resume name is only compared, never used in
		   a file access. JRA. */
		srvstr_pull_talloc(ctx, params, req->flags2,
				&resume_name, params+12,
				total_params - 12,
				STR_TERMINATE);

		if (!resume_name || !(ISDOT(resume_name) || ISDOTDOT(resume_name))) {
			reply_nterror(req, ntstatus);
			return;
		}
	}

	DEBUG(3,("call_trans2findnext: dirhandle = %d, max_data_bytes = %d, maxentries = %d, \
close_after_request=%d, close_if_end = %d requires_resume_key = %d \
resume_key = %d resume name = %s continue=%d level = %d\n",
		dptr_num, max_data_bytes, maxentries, close_after_request, close_if_end, 
		requires_resume_key, resume_key, resume_name, continue_bit, info_level));

	if (!maxentries) {
		/* W2K3 seems to treat zero as 1. */
		maxentries = 1;
	}

	switch (info_level) {
		case SMB_FIND_INFO_STANDARD:
		case SMB_FIND_EA_SIZE:
		case SMB_FIND_EA_LIST:
		case SMB_FIND_FILE_DIRECTORY_INFO:
		case SMB_FIND_FILE_FULL_DIRECTORY_INFO:
		case SMB_FIND_FILE_NAMES_INFO:
		case SMB_FIND_FILE_BOTH_DIRECTORY_INFO:
		case SMB_FIND_ID_FULL_DIRECTORY_INFO:
		case SMB_FIND_ID_BOTH_DIRECTORY_INFO:
			break;
		case SMB_FIND_FILE_UNIX:
		case SMB_FIND_FILE_UNIX_INFO2:
			/* Always use filesystem for UNIX mtime query. */
			ask_sharemode = false;
			if (!lp_unix_extensions()) {
				reply_nterror(req, NT_STATUS_INVALID_LEVEL);
				return;
			}
			break;
		default:
			reply_nterror(req, NT_STATUS_INVALID_LEVEL);
			return;
	}

	if (info_level == SMB_FIND_EA_LIST) {
		uint32 ea_size;

		if (total_data < 4) {
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}

		ea_size = IVAL(pdata,0);
		if (ea_size != total_data) {
			DEBUG(4,("call_trans2findnext: Rejecting EA request with incorrect \
total_data=%u (should be %u)\n", (unsigned int)total_data, (unsigned int)IVAL(pdata,0) ));
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}

		if (!lp_ea_support(SNUM(conn))) {
			reply_doserror(req, ERRDOS, ERReasnotsupported);
			return;
		}

		/* Pull out the list of names. */
		ea_list = read_ea_name_list(ctx, pdata + 4, ea_size - 4);
		if (!ea_list) {
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}
	}

	*ppdata = (char *)SMB_REALLOC(
		*ppdata, max_data_bytes + DIR_ENTRY_SAFETY_MARGIN);
	if(*ppdata == NULL) {
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}

	pdata = *ppdata;
	data_end = pdata + max_data_bytes + DIR_ENTRY_SAFETY_MARGIN - 1;

	/* Realloc the params space */
	*pparams = (char *)SMB_REALLOC(*pparams, 6*SIZEOFWORD);
	if(*pparams == NULL ) {
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}

	params = *pparams;

	/* Check that the dptr is valid */
	if(!(conn->dirptr = dptr_fetch_lanman2(dptr_num))) {
		reply_doserror(req, ERRDOS, ERRnofiles);
		return;
	}

	string_set(&conn->dirpath,dptr_path(dptr_num));

	/* Get the wildcard mask from the dptr */
	if((p = dptr_wcard(dptr_num))== NULL) {
		DEBUG(2,("dptr_num %d has no wildcard\n", dptr_num));
		reply_doserror(req, ERRDOS, ERRnofiles);
		return;
	}

	mask = p;
	directory = conn->dirpath;

	/* Get the attr mask from the dptr */
	dirtype = dptr_attr(dptr_num);

	DEBUG(3,("dptr_num is %d, mask = %s, attr = %x, dirptr=(0x%lX,%ld)\n",
		dptr_num, mask, dirtype,
		(long)conn->dirptr,
		dptr_TellDir(conn->dirptr)));

	/* Initialize per TRANS2_FIND_NEXT operation data */
	dptr_init_search_op(conn->dirptr);

	/* We don't need to check for VOL here as this is returned by
		a different TRANS2 call. */

	DEBUG(8,("dirpath=<%s> dontdescend=<%s>\n",conn->dirpath,lp_dontdescend(SNUM(conn))));
	if (in_list(conn->dirpath,lp_dontdescend(SNUM(conn)),conn->case_sensitive))
		dont_descend = True;

	p = pdata;
	space_remaining = max_data_bytes;
	out_of_space = False;

	/*
	 * Seek to the correct position. We no longer use the resume key but
	 * depend on the last file name instead.
	 */

	if(*resume_name && !continue_bit) {
		SMB_STRUCT_STAT st;

		long current_pos = 0;
		/*
		 * Remember, name_to_8_3 is called by
		 * get_lanman2_dir_entry(), so the resume name
		 * could be mangled. Ensure we check the unmangled name.
		 */

		if (mangle_is_mangled(resume_name, conn->params)) {
			char *new_resume_name = NULL;
			mangle_lookup_name_from_8_3(ctx,
						resume_name,
						&new_resume_name,
						conn->params);
			if (new_resume_name) {
				resume_name = new_resume_name;
			}
		}

		/*
		 * Fix for NT redirector problem triggered by resume key indexes
		 * changing between directory scans. We now return a resume key of 0
		 * and instead look for the filename to continue from (also given
		 * to us by NT/95/smbfs/smbclient). If no other scans have been done between the
		 * findfirst/findnext (as is usual) then the directory pointer
		 * should already be at the correct place.
		 */

		finished = !dptr_SearchDir(conn->dirptr, resume_name, &current_pos, &st);
	} /* end if resume_name && !continue_bit */

	for (i=0;(i<(int)maxentries) && !finished && !out_of_space ;i++) {
		bool got_exact_match = False;

		/* this is a heuristic to avoid seeking the dirptr except when 
			absolutely necessary. It allows for a filename of about 40 chars */
		if (space_remaining < DIRLEN_GUESS && numentries > 0) {
			out_of_space = True;
			finished = False;
		} else {
			finished = !get_lanman2_dir_entry(ctx,
						conn,
						req->flags2,
						mask,dirtype,info_level,
						requires_resume_key,dont_descend,
						ask_sharemode,
						&p,pdata,data_end,
						space_remaining, &out_of_space,
						&got_exact_match,
						&last_entry_off, ea_list);
		}

		if (finished && out_of_space)
			finished = False;

		if (!finished && !out_of_space)
			numentries++;

		/*
		 * As an optimisation if we know we aren't looking
		 * for a wildcard name (ie. the name matches the wildcard exactly)
		 * then we can finish on any (first) match.
		 * This speeds up large directory searches. JRA.
		 */

		if(got_exact_match)
			finished = True;

		space_remaining = max_data_bytes - PTR_DIFF(p,pdata);
	}

	DEBUG( 3, ( "%s mask=%s directory=%s dirtype=%d numentries=%d\n",
		smb_fn_name(req->cmd),
		mask, directory, dirtype, numentries ) );

	/* Check if we can close the dirptr */
	if(close_after_request || (finished && close_if_end)) {
		DEBUG(5,("call_trans2findnext: closing dptr_num = %d\n", dptr_num));
		dptr_close(&dptr_num); /* This frees up the saved mask */
	}

	/* Set up the return parameter block */
	SSVAL(params,0,numentries);
	SSVAL(params,2,finished);
	SSVAL(params,4,0); /* Never an EA error */
	SSVAL(params,6,last_entry_off);

	send_trans2_replies(conn, req, params, 8, pdata, PTR_DIFF(p,pdata),
			    max_data_bytes);

	return;
}

unsigned char *create_volume_objectid(connection_struct *conn, unsigned char objid[16])
{
	E_md4hash(lp_servicename(SNUM(conn)),objid);
	return objid;
}

static void samba_extended_info_version(struct smb_extended_info *extended_info)
{
	SMB_ASSERT(extended_info != NULL);

	extended_info->samba_magic = SAMBA_EXTENDED_INFO_MAGIC;
	extended_info->samba_version = ((SAMBA_VERSION_MAJOR & 0xff) << 24)
				       | ((SAMBA_VERSION_MINOR & 0xff) << 16)
				       | ((SAMBA_VERSION_RELEASE & 0xff) << 8);
#ifdef SAMBA_VERSION_REVISION
	extended_info->samba_version |= (tolower(*SAMBA_VERSION_REVISION) - 'a' + 1) & 0xff;
#endif
	extended_info->samba_subversion = 0;
#ifdef SAMBA_VERSION_RC_RELEASE
	extended_info->samba_subversion |= (SAMBA_VERSION_RC_RELEASE & 0xff) << 24;
#else
#ifdef SAMBA_VERSION_PRE_RELEASE
	extended_info->samba_subversion |= (SAMBA_VERSION_PRE_RELEASE & 0xff) << 16;
#endif
#endif
#ifdef SAMBA_VERSION_VENDOR_PATCH
	extended_info->samba_subversion |= (SAMBA_VERSION_VENDOR_PATCH & 0xffff);
#endif
	extended_info->samba_gitcommitdate = 0;
#ifdef SAMBA_VERSION_GIT_COMMIT_TIME
	unix_to_nt_time(&extended_info->samba_gitcommitdate, SAMBA_VERSION_GIT_COMMIT_TIME);
#endif

	memset(extended_info->samba_version_string, 0,
	       sizeof(extended_info->samba_version_string));

	snprintf (extended_info->samba_version_string,
		  sizeof(extended_info->samba_version_string),
		  "%s", samba_version_string());
}

/****************************************************************************
 Reply to a TRANS2_QFSINFO (query filesystem info).
****************************************************************************/

static void call_trans2qfsinfo(connection_struct *conn,
			       struct smb_request *req,
			       char **pparams, int total_params,
			       char **ppdata, int total_data,
			       unsigned int max_data_bytes)
{
	char *pdata, *end_data;
	char *params = *pparams;
	uint16 info_level;
	int data_len, len;
	SMB_STRUCT_STAT st;
	const char *vname = volume_label(SNUM(conn));
	int snum = SNUM(conn);
	char *fstype = lp_fstype(SNUM(conn));
	uint32 additional_flags = 0;
	
	if (total_params < 2) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	info_level = SVAL(params,0);

	if (IS_IPC(conn)) {
		if (info_level != SMB_QUERY_CIFS_UNIX_INFO) {
			DEBUG(0,("call_trans2qfsinfo: not an allowed "
				"info level (0x%x) on IPC$.\n",
				(unsigned int)info_level));
			reply_nterror(req, NT_STATUS_ACCESS_DENIED);
			return;
		}
	}

	if (ENCRYPTION_REQUIRED(conn) && !req->encrypted) {
		if (info_level != SMB_QUERY_CIFS_UNIX_INFO) {
			DEBUG(0,("call_trans2qfsinfo: encryption required "
				"and info level 0x%x sent.\n",
				(unsigned int)info_level));
			exit_server_cleanly("encryption required "
				"on connection");
			return;
		}
	}

	DEBUG(3,("call_trans2qfsinfo: level = %d\n", info_level));

	if(SMB_VFS_STAT(conn,".",&st)!=0) {
		DEBUG(2,("call_trans2qfsinfo: stat of . failed (%s)\n", strerror(errno)));
		reply_doserror(req, ERRSRV, ERRinvdevice);
		return;
	}

	*ppdata = (char *)SMB_REALLOC(
		*ppdata, max_data_bytes + DIR_ENTRY_SAFETY_MARGIN);
	if (*ppdata == NULL ) {
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}

	pdata = *ppdata;
	memset((char *)pdata,'\0',max_data_bytes + DIR_ENTRY_SAFETY_MARGIN);
	end_data = pdata + max_data_bytes + DIR_ENTRY_SAFETY_MARGIN - 1;

	switch (info_level) {
		case SMB_INFO_ALLOCATION:
		{
			uint64_t dfree,dsize,bsize,block_size,sectors_per_unit,bytes_per_sector;
			data_len = 18;
			if (get_dfree_info(conn,".",False,&bsize,&dfree,&dsize) == (uint64_t)-1) {
				reply_unixerror(req, ERRHRD, ERRgeneral);
				return;
			}

			block_size = lp_block_size(snum);
			if (bsize < block_size) {
				uint64_t factor = block_size/bsize;
				bsize = block_size;
				dsize /= factor;
				dfree /= factor;
			}
			if (bsize > block_size) {
				uint64_t factor = bsize/block_size;
				bsize = block_size;
				dsize *= factor;
				dfree *= factor;
			}
			bytes_per_sector = 512;
			sectors_per_unit = bsize/bytes_per_sector;

			DEBUG(5,("call_trans2qfsinfo : SMB_INFO_ALLOCATION id=%x, bsize=%u, cSectorUnit=%u, \
cBytesSector=%u, cUnitTotal=%u, cUnitAvail=%d\n", (unsigned int)st.st_dev, (unsigned int)bsize, (unsigned int)sectors_per_unit,
				(unsigned int)bytes_per_sector, (unsigned int)dsize, (unsigned int)dfree));

			SIVAL(pdata,l1_idFileSystem,st.st_dev);
			SIVAL(pdata,l1_cSectorUnit,sectors_per_unit);
			SIVAL(pdata,l1_cUnit,dsize);
			SIVAL(pdata,l1_cUnitAvail,dfree);
			SSVAL(pdata,l1_cbSector,bytes_per_sector);
			break;
		}

		case SMB_INFO_VOLUME:
			/* Return volume name */
			/* 
			 * Add volume serial number - hash of a combination of
			 * the called hostname and the service name.
			 */
			SIVAL(pdata,0,str_checksum(lp_servicename(snum)) ^ (str_checksum(get_local_machine_name())<<16) );
			/*
			 * Win2k3 and previous mess this up by sending a name length
			 * one byte short. I believe only older clients (OS/2 Win9x) use
			 * this call so try fixing this by adding a terminating null to
			 * the pushed string. The change here was adding the STR_TERMINATE. JRA.
			 */
			len = srvstr_push(
				pdata, req->flags2,
				pdata+l2_vol_szVolLabel, vname,
				PTR_DIFF(end_data, pdata+l2_vol_szVolLabel),
				STR_NOALIGN|STR_TERMINATE);
			SCVAL(pdata,l2_vol_cch,len);
			data_len = l2_vol_szVolLabel + len;
			DEBUG(5,("call_trans2qfsinfo : time = %x, namelen = %d, name = %s\n",
				(unsigned)st.st_ctime, len, vname));
			break;

		case SMB_QUERY_FS_ATTRIBUTE_INFO:
		case SMB_FS_ATTRIBUTE_INFORMATION:

			additional_flags = 0;
#if defined(HAVE_SYS_QUOTAS)
			additional_flags |= FILE_VOLUME_QUOTAS;
#endif

			if(lp_nt_acl_support(SNUM(conn))) {
				additional_flags |= FILE_PERSISTENT_ACLS;
			}

			/* Capabilities are filled in at connection time through STATVFS call */
			additional_flags |= conn->fs_capabilities;

			SIVAL(pdata,0,FILE_CASE_PRESERVED_NAMES|FILE_CASE_SENSITIVE_SEARCH|
				FILE_SUPPORTS_OBJECT_IDS|FILE_UNICODE_ON_DISK|
				additional_flags); /* FS ATTRIBUTES */

			SIVAL(pdata,4,255); /* Max filename component length */
			/* NOTE! the fstype must *not* be null terminated or win98 won't recognise it
				and will think we can't do long filenames */
			len = srvstr_push(pdata, req->flags2, pdata+12, fstype,
					  PTR_DIFF(end_data, pdata+12),
					  STR_UNICODE);
			SIVAL(pdata,8,len);
			data_len = 12 + len;
			break;

		case SMB_QUERY_FS_LABEL_INFO:
		case SMB_FS_LABEL_INFORMATION:
			len = srvstr_push(pdata, req->flags2, pdata+4, vname,
					  PTR_DIFF(end_data, pdata+4), 0);
			data_len = 4 + len;
			SIVAL(pdata,0,len);
			break;

		case SMB_QUERY_FS_VOLUME_INFO:      
		case SMB_FS_VOLUME_INFORMATION:

			/* 
			 * Add volume serial number - hash of a combination of
			 * the called hostname and the service name.
			 */
			SIVAL(pdata,8,str_checksum(lp_servicename(snum)) ^ 
				(str_checksum(get_local_machine_name())<<16));

			/* Max label len is 32 characters. */
			len = srvstr_push(pdata, req->flags2, pdata+18, vname,
					  PTR_DIFF(end_data, pdata+18),
					  STR_UNICODE);
			SIVAL(pdata,12,len);
			data_len = 18+len;

			DEBUG(5,("call_trans2qfsinfo : SMB_QUERY_FS_VOLUME_INFO namelen = %d, vol=%s serv=%s\n", 
				(int)strlen(vname),vname, lp_servicename(snum)));
			break;

		case SMB_QUERY_FS_SIZE_INFO:
		case SMB_FS_SIZE_INFORMATION:
		{
			uint64_t dfree,dsize,bsize,block_size,sectors_per_unit,bytes_per_sector;
			data_len = 24;
			if (get_dfree_info(conn,".",False,&bsize,&dfree,&dsize) == (uint64_t)-1) {
				reply_unixerror(req, ERRHRD, ERRgeneral);
				return;
			}
			block_size = lp_block_size(snum);
			if (bsize < block_size) {
				uint64_t factor = block_size/bsize;
				bsize = block_size;
				dsize /= factor;
				dfree /= factor;
			}
			if (bsize > block_size) {
				uint64_t factor = bsize/block_size;
				bsize = block_size;
				dsize *= factor;
				dfree *= factor;
			}
			bytes_per_sector = 512;
			sectors_per_unit = bsize/bytes_per_sector;
			DEBUG(5,("call_trans2qfsinfo : SMB_QUERY_FS_SIZE_INFO bsize=%u, cSectorUnit=%u, \
cBytesSector=%u, cUnitTotal=%u, cUnitAvail=%d\n", (unsigned int)bsize, (unsigned int)sectors_per_unit,
				(unsigned int)bytes_per_sector, (unsigned int)dsize, (unsigned int)dfree));
			SBIG_UINT(pdata,0,dsize);
			SBIG_UINT(pdata,8,dfree);
			SIVAL(pdata,16,sectors_per_unit);
			SIVAL(pdata,20,bytes_per_sector);
			break;
		}

		case SMB_FS_FULL_SIZE_INFORMATION:
		{
			uint64_t dfree,dsize,bsize,block_size,sectors_per_unit,bytes_per_sector;
			data_len = 32;
			if (get_dfree_info(conn,".",False,&bsize,&dfree,&dsize) == (uint64_t)-1) {
				reply_unixerror(req, ERRHRD, ERRgeneral);
				return;
			}
			block_size = lp_block_size(snum);
			if (bsize < block_size) {
				uint64_t factor = block_size/bsize;
				bsize = block_size;
				dsize /= factor;
				dfree /= factor;
			}
			if (bsize > block_size) {
				uint64_t factor = bsize/block_size;
				bsize = block_size;
				dsize *= factor;
				dfree *= factor;
			}
			bytes_per_sector = 512;
			sectors_per_unit = bsize/bytes_per_sector;
			DEBUG(5,("call_trans2qfsinfo : SMB_QUERY_FS_FULL_SIZE_INFO bsize=%u, cSectorUnit=%u, \
cBytesSector=%u, cUnitTotal=%u, cUnitAvail=%d\n", (unsigned int)bsize, (unsigned int)sectors_per_unit,
				(unsigned int)bytes_per_sector, (unsigned int)dsize, (unsigned int)dfree));
			SBIG_UINT(pdata,0,dsize); /* Total Allocation units. */
			SBIG_UINT(pdata,8,dfree); /* Caller available allocation units. */
			SBIG_UINT(pdata,16,dfree); /* Actual available allocation units. */
			SIVAL(pdata,24,sectors_per_unit); /* Sectors per allocation unit. */
			SIVAL(pdata,28,bytes_per_sector); /* Bytes per sector. */
			break;
		}

		case SMB_QUERY_FS_DEVICE_INFO:
		case SMB_FS_DEVICE_INFORMATION:
			data_len = 8;
			SIVAL(pdata,0,0); /* dev type */
			SIVAL(pdata,4,0); /* characteristics */
			break;

#ifdef HAVE_SYS_QUOTAS
		case SMB_FS_QUOTA_INFORMATION:
		/* 
		 * what we have to send --metze:
		 *
		 * Unknown1: 		24 NULL bytes
		 * Soft Quota Treshold: 8 bytes seems like uint64_t or so
		 * Hard Quota Limit:	8 bytes seems like uint64_t or so
		 * Quota Flags:		2 byte :
		 * Unknown3:		6 NULL bytes
		 *
		 * 48 bytes total
		 * 
		 * details for Quota Flags:
		 * 
		 * 0x0020 Log Limit: log if the user exceeds his Hard Quota
		 * 0x0010 Log Warn:  log if the user exceeds his Soft Quota
		 * 0x0002 Deny Disk: deny disk access when the user exceeds his Hard Quota
		 * 0x0001 Enable Quotas: enable quota for this fs
		 *
		 */
		{
			/* we need to fake up a fsp here,
			 * because its not send in this call
			 */
			files_struct fsp;
			SMB_NTQUOTA_STRUCT quotas;
			
			ZERO_STRUCT(fsp);
			ZERO_STRUCT(quotas);
			
			fsp.conn = conn;
			fsp.fnum = -1;
			
			/* access check */
			if (conn->server_info->utok.uid != 0) {
				DEBUG(0,("set_user_quota: access_denied "
					 "service [%s] user [%s]\n",
					 lp_servicename(SNUM(conn)),
					 conn->server_info->unix_name));
				reply_doserror(req, ERRDOS, ERRnoaccess);
				return;
			}
			
			if (vfs_get_ntquota(&fsp, SMB_USER_FS_QUOTA_TYPE, NULL, &quotas)!=0) {
				DEBUG(0,("vfs_get_ntquota() failed for service [%s]\n",lp_servicename(SNUM(conn))));
				reply_doserror(req, ERRSRV, ERRerror);
				return;
			}

			data_len = 48;

			DEBUG(10,("SMB_FS_QUOTA_INFORMATION: for service [%s]\n",lp_servicename(SNUM(conn))));		
		
			/* Unknown1 24 NULL bytes*/
			SBIG_UINT(pdata,0,(uint64_t)0);
			SBIG_UINT(pdata,8,(uint64_t)0);
			SBIG_UINT(pdata,16,(uint64_t)0);
		
			/* Default Soft Quota 8 bytes */
			SBIG_UINT(pdata,24,quotas.softlim);

			/* Default Hard Quota 8 bytes */
			SBIG_UINT(pdata,32,quotas.hardlim);
	
			/* Quota flag 2 bytes */
			SSVAL(pdata,40,quotas.qflags);
		
			/* Unknown3 6 NULL bytes */
			SSVAL(pdata,42,0);
			SIVAL(pdata,44,0);
			
			break;
		}
#endif /* HAVE_SYS_QUOTAS */
		case SMB_FS_OBJECTID_INFORMATION:
		{
			unsigned char objid[16];
			struct smb_extended_info extended_info;
			memcpy(pdata,create_volume_objectid(conn, objid),16);
			samba_extended_info_version (&extended_info);
			SIVAL(pdata,16,extended_info.samba_magic);
			SIVAL(pdata,20,extended_info.samba_version);
			SIVAL(pdata,24,extended_info.samba_subversion);
			SBIG_UINT(pdata,28,extended_info.samba_gitcommitdate);
			memcpy(pdata+36,extended_info.samba_version_string,28);
			data_len = 64;
			break;
		}

		/*
		 * Query the version and capabilities of the CIFS UNIX extensions
		 * in use.
		 */

		case SMB_QUERY_CIFS_UNIX_INFO:
		{
			bool large_write = lp_min_receive_file_size() &&
						!srv_is_signing_active();
			bool large_read = !srv_is_signing_active();
			int encrypt_caps = 0;

			if (!lp_unix_extensions()) {
				reply_nterror(req, NT_STATUS_INVALID_LEVEL);
				return;
			}

			switch (conn->encrypt_level) {
			case 0:
				encrypt_caps = 0;
				break;
			case 1:
			case Auto:
				encrypt_caps = CIFS_UNIX_TRANSPORT_ENCRYPTION_CAP;
				break;
			case Required:
				encrypt_caps = CIFS_UNIX_TRANSPORT_ENCRYPTION_CAP|
						CIFS_UNIX_TRANSPORT_ENCRYPTION_MANDATORY_CAP;
				large_write = false;
				large_read = false;
				break;
			}

			data_len = 12;
			SSVAL(pdata,0,CIFS_UNIX_MAJOR_VERSION);
			SSVAL(pdata,2,CIFS_UNIX_MINOR_VERSION);

			/* We have POSIX ACLs, pathname, encryption, 
			 * large read/write, and locking capability. */

			SBIG_UINT(pdata,4,((uint64_t)(
					CIFS_UNIX_POSIX_ACLS_CAP|
					CIFS_UNIX_POSIX_PATHNAMES_CAP|
					CIFS_UNIX_FCNTL_LOCKS_CAP|
					CIFS_UNIX_EXTATTR_CAP|
					CIFS_UNIX_POSIX_PATH_OPERATIONS_CAP|
					encrypt_caps|
					(large_read ? CIFS_UNIX_LARGE_READ_CAP : 0) |
					(large_write ?
					CIFS_UNIX_LARGE_WRITE_CAP : 0))));
			break;
		}

		case SMB_QUERY_POSIX_FS_INFO:
		{
			int rc;
			vfs_statvfs_struct svfs;

			if (!lp_unix_extensions()) {
				reply_nterror(req, NT_STATUS_INVALID_LEVEL);
				return;
			}

			rc = SMB_VFS_STATVFS(conn, ".", &svfs);

			if (!rc) {
				data_len = 56;
				SIVAL(pdata,0,svfs.OptimalTransferSize);
				SIVAL(pdata,4,svfs.BlockSize);
				SBIG_UINT(pdata,8,svfs.TotalBlocks);
				SBIG_UINT(pdata,16,svfs.BlocksAvail);
				SBIG_UINT(pdata,24,svfs.UserBlocksAvail);
				SBIG_UINT(pdata,32,svfs.TotalFileNodes);
				SBIG_UINT(pdata,40,svfs.FreeFileNodes);
				SBIG_UINT(pdata,48,svfs.FsIdentifier);
				DEBUG(5,("call_trans2qfsinfo : SMB_QUERY_POSIX_FS_INFO succsessful\n"));
#ifdef EOPNOTSUPP
			} else if (rc == EOPNOTSUPP) {
				reply_nterror(req, NT_STATUS_INVALID_LEVEL);
				return;
#endif /* EOPNOTSUPP */
			} else {
				DEBUG(0,("vfs_statvfs() failed for service [%s]\n",lp_servicename(SNUM(conn))));
				reply_doserror(req, ERRSRV, ERRerror);
				return;
			}
			break;
		}

		case SMB_QUERY_POSIX_WHOAMI:
		{
			uint32_t flags = 0;
			uint32_t sid_bytes;
			int i;

			if (!lp_unix_extensions()) {
				reply_nterror(req, NT_STATUS_INVALID_LEVEL);
				return;
			}

			if (max_data_bytes < 40) {
				reply_nterror(req, NT_STATUS_BUFFER_TOO_SMALL);
				return;
			}

			/* We ARE guest if global_sid_Builtin_Guests is
			 * in our list of SIDs.
			 */
			if (nt_token_check_sid(&global_sid_Builtin_Guests,
					       conn->server_info->ptok)) {
				flags |= SMB_WHOAMI_GUEST;
			}

			/* We are NOT guest if global_sid_Authenticated_Users
			 * is in our list of SIDs.
			 */
			if (nt_token_check_sid(&global_sid_Authenticated_Users,
					       conn->server_info->ptok)) {
				flags &= ~SMB_WHOAMI_GUEST;
			}

			/* NOTE: 8 bytes for UID/GID, irrespective of native
			 * platform size. This matches
			 * SMB_QUERY_FILE_UNIX_BASIC and friends.
			 */
			data_len = 4 /* flags */
			    + 4 /* flag mask */
			    + 8 /* uid */
			    + 8 /* gid */
			    + 4 /* ngroups */
			    + 4 /* num_sids */
			    + 4 /* SID bytes */
			    + 4 /* pad/reserved */
			    + (conn->server_info->utok.ngroups * 8)
				/* groups list */
			    + (conn->server_info->ptok->num_sids *
				    SID_MAX_SIZE)
				/* SID list */;

			SIVAL(pdata, 0, flags);
			SIVAL(pdata, 4, SMB_WHOAMI_MASK);
			SBIG_UINT(pdata, 8,
				  (uint64_t)conn->server_info->utok.uid);
			SBIG_UINT(pdata, 16,
				  (uint64_t)conn->server_info->utok.gid);


			if (data_len >= max_data_bytes) {
				/* Potential overflow, skip the GIDs and SIDs. */

				SIVAL(pdata, 24, 0); /* num_groups */
				SIVAL(pdata, 28, 0); /* num_sids */
				SIVAL(pdata, 32, 0); /* num_sid_bytes */
				SIVAL(pdata, 36, 0); /* reserved */

				data_len = 40;
				break;
			}

			SIVAL(pdata, 24, conn->server_info->utok.ngroups);
			SIVAL(pdata, 28, conn->server_info->num_sids);

			/* We walk the SID list twice, but this call is fairly
			 * infrequent, and I don't expect that it's performance
			 * sensitive -- jpeach
			 */
			for (i = 0, sid_bytes = 0;
			     i < conn->server_info->ptok->num_sids; ++i) {
				sid_bytes += ndr_size_dom_sid(
					&conn->server_info->ptok->user_sids[i],
					NULL, 
					0);
			}

			/* SID list byte count */
			SIVAL(pdata, 32, sid_bytes);

			/* 4 bytes pad/reserved - must be zero */
			SIVAL(pdata, 36, 0);
			data_len = 40;

			/* GID list */
			for (i = 0; i < conn->server_info->utok.ngroups; ++i) {
				SBIG_UINT(pdata, data_len,
					  (uint64_t)conn->server_info->utok.groups[i]);
				data_len += 8;
			}

			/* SID list */
			for (i = 0;
			    i < conn->server_info->ptok->num_sids; ++i) {
				int sid_len = ndr_size_dom_sid(
					&conn->server_info->ptok->user_sids[i],
					NULL,
					0);

				sid_linearize(pdata + data_len, sid_len,
				    &conn->server_info->ptok->user_sids[i]);
				data_len += sid_len;
			}

			break;
		}

		case SMB_MAC_QUERY_FS_INFO:
			/*
			 * Thursby MAC extension... ONLY on NTFS filesystems
			 * once we do streams then we don't need this
			 */
			if (strequal(lp_fstype(SNUM(conn)),"NTFS")) {
				data_len = 88;
				SIVAL(pdata,84,0x100); /* Don't support mac... */
				break;
			}
			/* drop through */
		default:
			reply_nterror(req, NT_STATUS_INVALID_LEVEL);
			return;
	}


	send_trans2_replies(conn, req, params, 0, pdata, data_len,
			    max_data_bytes);

	DEBUG( 4, ( "%s info_level = %d\n",
		    smb_fn_name(req->cmd), info_level) );

	return;
}

/****************************************************************************
 Reply to a TRANS2_SETFSINFO (set filesystem info).
****************************************************************************/

static void call_trans2setfsinfo(connection_struct *conn,
				 struct smb_request *req,
				 char **pparams, int total_params,
				 char **ppdata, int total_data,
				 unsigned int max_data_bytes)
{
	char *pdata = *ppdata;
	char *params = *pparams;
	uint16 info_level;

	DEBUG(10,("call_trans2setfsinfo: for service [%s]\n",lp_servicename(SNUM(conn))));

	/*  */
	if (total_params < 4) {
		DEBUG(0,("call_trans2setfsinfo: requires total_params(%d) >= 4 bytes!\n",
			total_params));
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	info_level = SVAL(params,2);

	if (IS_IPC(conn)) {
		if (info_level != SMB_REQUEST_TRANSPORT_ENCRYPTION &&
				info_level != SMB_SET_CIFS_UNIX_INFO) {
			DEBUG(0,("call_trans2setfsinfo: not an allowed "
				"info level (0x%x) on IPC$.\n",
				(unsigned int)info_level));
			reply_nterror(req, NT_STATUS_ACCESS_DENIED);
			return;
		}
	}

	if (ENCRYPTION_REQUIRED(conn) && !req->encrypted) {
		if (info_level != SMB_REQUEST_TRANSPORT_ENCRYPTION) {
			DEBUG(0,("call_trans2setfsinfo: encryption required "
				"and info level 0x%x sent.\n",
				(unsigned int)info_level));
			exit_server_cleanly("encryption required "
				"on connection");
			return;
		}
	}

	switch(info_level) {
		case SMB_SET_CIFS_UNIX_INFO:
			{
				uint16 client_unix_major;
				uint16 client_unix_minor;
				uint32 client_unix_cap_low;
				uint32 client_unix_cap_high;

				if (!lp_unix_extensions()) {
					reply_nterror(req,
						      NT_STATUS_INVALID_LEVEL);
					return;
				}

				/* There should be 12 bytes of capabilities set. */
				if (total_data < 8) {
					reply_nterror(
						req,
						NT_STATUS_INVALID_PARAMETER);
					return;
				}
				client_unix_major = SVAL(pdata,0);
				client_unix_minor = SVAL(pdata,2);
				client_unix_cap_low = IVAL(pdata,4);
				client_unix_cap_high = IVAL(pdata,8);
				/* Just print these values for now. */
				DEBUG(10,("call_trans2setfsinfo: set unix info. major = %u, minor = %u \
cap_low = 0x%x, cap_high = 0x%x\n",
					(unsigned int)client_unix_major,
					(unsigned int)client_unix_minor,
					(unsigned int)client_unix_cap_low,
					(unsigned int)client_unix_cap_high ));

				/* Here is where we must switch to posix pathname processing... */
				if (client_unix_cap_low & CIFS_UNIX_POSIX_PATHNAMES_CAP) {
					lp_set_posix_pathnames();
					mangle_change_to_posix();
				}

				if ((client_unix_cap_low & CIFS_UNIX_FCNTL_LOCKS_CAP) &&
				    !(client_unix_cap_low & CIFS_UNIX_POSIX_PATH_OPERATIONS_CAP)) {
					/* Client that knows how to do posix locks,
					 * but not posix open/mkdir operations. Set a
					 * default type for read/write checks. */

					lp_set_posix_default_cifsx_readwrite_locktype(POSIX_LOCK);

				}
				break;
			}

		case SMB_REQUEST_TRANSPORT_ENCRYPTION:
			{
				NTSTATUS status;
				size_t param_len = 0;
				size_t data_len = total_data;

				if (!lp_unix_extensions()) {
					reply_nterror(
						req,
						NT_STATUS_INVALID_LEVEL);
					return;
				}

				if (lp_smb_encrypt(SNUM(conn)) == false) {
					reply_nterror(
						req,
						NT_STATUS_NOT_SUPPORTED);
					return;
				}

				DEBUG( 4,("call_trans2setfsinfo: "
					"request transport encryption.\n"));

				status = srv_request_encryption_setup(conn,
								(unsigned char **)ppdata,
								&data_len,
								(unsigned char **)pparams,
								&param_len);

				if (!NT_STATUS_EQUAL(status, NT_STATUS_MORE_PROCESSING_REQUIRED) &&
						!NT_STATUS_IS_OK(status)) {
					reply_nterror(req, status);
					return;
				}

				send_trans2_replies(conn, req,
						*pparams,
						param_len,
						*ppdata,
						data_len,
						max_data_bytes);

				if (NT_STATUS_IS_OK(status)) {
					/* Server-side transport
					 * encryption is now *on*. */
					status = srv_encryption_start(conn);
					if (!NT_STATUS_IS_OK(status)) {
						exit_server_cleanly(
							"Failure in setting "
							"up encrypted transport");
					}
				}
				return;
			}

		case SMB_FS_QUOTA_INFORMATION:
			{
				files_struct *fsp = NULL;
				SMB_NTQUOTA_STRUCT quotas;
	
				ZERO_STRUCT(quotas);

				/* access check */
				if ((conn->server_info->utok.uid != 0)
				    ||!CAN_WRITE(conn)) {
					DEBUG(0,("set_user_quota: access_denied service [%s] user [%s]\n",
						 lp_servicename(SNUM(conn)),
						 conn->server_info->unix_name));
					reply_doserror(req, ERRSRV, ERRaccess);
					return;
				}

				/* note: normaly there're 48 bytes,
				 * but we didn't use the last 6 bytes for now 
				 * --metze 
				 */
				fsp = file_fsp(req, SVAL(params,0));

				if (!check_fsp_ntquota_handle(conn, req,
							      fsp)) {
					DEBUG(3,("TRANSACT_GET_USER_QUOTA: no valid QUOTA HANDLE\n"));
					reply_nterror(
						req, NT_STATUS_INVALID_HANDLE);
					return;
				}

				if (total_data < 42) {
					DEBUG(0,("call_trans2setfsinfo: SET_FS_QUOTA: requires total_data(%d) >= 42 bytes!\n",
						total_data));
					reply_nterror(
						req,
						NT_STATUS_INVALID_PARAMETER);
					return;
				}
			
				/* unknown_1 24 NULL bytes in pdata*/
		
				/* the soft quotas 8 bytes (uint64_t)*/
				quotas.softlim = (uint64_t)IVAL(pdata,24);
#ifdef LARGE_SMB_OFF_T
				quotas.softlim |= (((uint64_t)IVAL(pdata,28)) << 32);
#else /* LARGE_SMB_OFF_T */
				if ((IVAL(pdata,28) != 0)&&
					((quotas.softlim != 0xFFFFFFFF)||
					(IVAL(pdata,28)!=0xFFFFFFFF))) {
					/* more than 32 bits? */
					reply_nterror(
						req,
						NT_STATUS_INVALID_PARAMETER);
					return;
				}
#endif /* LARGE_SMB_OFF_T */
		
				/* the hard quotas 8 bytes (uint64_t)*/
				quotas.hardlim = (uint64_t)IVAL(pdata,32);
#ifdef LARGE_SMB_OFF_T
				quotas.hardlim |= (((uint64_t)IVAL(pdata,36)) << 32);
#else /* LARGE_SMB_OFF_T */
				if ((IVAL(pdata,36) != 0)&&
					((quotas.hardlim != 0xFFFFFFFF)||
					(IVAL(pdata,36)!=0xFFFFFFFF))) {
					/* more than 32 bits? */
					reply_nterror(
						req,
						NT_STATUS_INVALID_PARAMETER);
					return;
				}
#endif /* LARGE_SMB_OFF_T */
		
				/* quota_flags 2 bytes **/
				quotas.qflags = SVAL(pdata,40);
		
				/* unknown_2 6 NULL bytes follow*/
		
				/* now set the quotas */
				if (vfs_set_ntquota(fsp, SMB_USER_FS_QUOTA_TYPE, NULL, &quotas)!=0) {
					DEBUG(0,("vfs_set_ntquota() failed for service [%s]\n",lp_servicename(SNUM(conn))));
					reply_doserror(req, ERRSRV, ERRerror);
					return;
				}
			
				break;
			}
		default:
			DEBUG(3,("call_trans2setfsinfo: unknown level (0x%X) not implemented yet.\n",
				info_level));
			reply_nterror(req, NT_STATUS_INVALID_LEVEL);
			return;
			break;
	}

	/* 
	 * sending this reply works fine, 
	 * but I'm not sure it's the same 
	 * like windows do...
	 * --metze
	 */
	reply_outbuf(req, 10, 0);
}

#if defined(HAVE_POSIX_ACLS)
/****************************************************************************
 Utility function to count the number of entries in a POSIX acl.
****************************************************************************/

static unsigned int count_acl_entries(connection_struct *conn, SMB_ACL_T posix_acl)
{
	unsigned int ace_count = 0;
	int entry_id = SMB_ACL_FIRST_ENTRY;
	SMB_ACL_ENTRY_T entry;

	while ( posix_acl && (SMB_VFS_SYS_ACL_GET_ENTRY(conn, posix_acl, entry_id, &entry) == 1)) {
		/* get_next... */
		if (entry_id == SMB_ACL_FIRST_ENTRY) {
			entry_id = SMB_ACL_NEXT_ENTRY;
		}
		ace_count++;
	}
	return ace_count;
}

/****************************************************************************
 Utility function to marshall a POSIX acl into wire format.
****************************************************************************/

static bool marshall_posix_acl(connection_struct *conn, char *pdata, SMB_STRUCT_STAT *pst, SMB_ACL_T posix_acl)
{
	int entry_id = SMB_ACL_FIRST_ENTRY;
	SMB_ACL_ENTRY_T entry;

	while ( posix_acl && (SMB_VFS_SYS_ACL_GET_ENTRY(conn, posix_acl, entry_id, &entry) == 1)) {
		SMB_ACL_TAG_T tagtype;
		SMB_ACL_PERMSET_T permset;
		unsigned char perms = 0;
		unsigned int own_grp;

		/* get_next... */
		if (entry_id == SMB_ACL_FIRST_ENTRY) {
			entry_id = SMB_ACL_NEXT_ENTRY;
		}

		if (SMB_VFS_SYS_ACL_GET_TAG_TYPE(conn, entry, &tagtype) == -1) {
			DEBUG(0,("marshall_posix_acl: SMB_VFS_SYS_ACL_GET_TAG_TYPE failed.\n"));
			return False;
		}

		if (SMB_VFS_SYS_ACL_GET_PERMSET(conn, entry, &permset) == -1) {
			DEBUG(0,("marshall_posix_acl: SMB_VFS_SYS_ACL_GET_PERMSET failed.\n"));
			return False;
		}

		perms |= (SMB_VFS_SYS_ACL_GET_PERM(conn, permset, SMB_ACL_READ) ? SMB_POSIX_ACL_READ : 0);
		perms |= (SMB_VFS_SYS_ACL_GET_PERM(conn, permset, SMB_ACL_WRITE) ? SMB_POSIX_ACL_WRITE : 0);
		perms |= (SMB_VFS_SYS_ACL_GET_PERM(conn, permset, SMB_ACL_EXECUTE) ? SMB_POSIX_ACL_EXECUTE : 0);

		SCVAL(pdata,1,perms);

		switch (tagtype) {
			case SMB_ACL_USER_OBJ:
				SCVAL(pdata,0,SMB_POSIX_ACL_USER_OBJ);
				own_grp = (unsigned int)pst->st_uid;
				SIVAL(pdata,2,own_grp);
				SIVAL(pdata,6,0);
				break;
			case SMB_ACL_USER:
				{
					uid_t *puid = (uid_t *)SMB_VFS_SYS_ACL_GET_QUALIFIER(conn, entry);
					if (!puid) {
						DEBUG(0,("marshall_posix_acl: SMB_VFS_SYS_ACL_GET_QUALIFIER failed.\n"));
						return False;
					}
					own_grp = (unsigned int)*puid;
					SMB_VFS_SYS_ACL_FREE_QUALIFIER(conn, (void *)puid,tagtype);
					SCVAL(pdata,0,SMB_POSIX_ACL_USER);
					SIVAL(pdata,2,own_grp);
					SIVAL(pdata,6,0);
					break;
				}
			case SMB_ACL_GROUP_OBJ:
				SCVAL(pdata,0,SMB_POSIX_ACL_GROUP_OBJ);
				own_grp = (unsigned int)pst->st_gid;
				SIVAL(pdata,2,own_grp);
				SIVAL(pdata,6,0);
				break;
			case SMB_ACL_GROUP:
				{
					gid_t *pgid= (gid_t *)SMB_VFS_SYS_ACL_GET_QUALIFIER(conn, entry);
					if (!pgid) {
						DEBUG(0,("marshall_posix_acl: SMB_VFS_SYS_ACL_GET_QUALIFIER failed.\n"));
						return False;
					}
					own_grp = (unsigned int)*pgid;
					SMB_VFS_SYS_ACL_FREE_QUALIFIER(conn, (void *)pgid,tagtype);
					SCVAL(pdata,0,SMB_POSIX_ACL_GROUP);
					SIVAL(pdata,2,own_grp);
					SIVAL(pdata,6,0);
					break;
				}
			case SMB_ACL_MASK:
				SCVAL(pdata,0,SMB_POSIX_ACL_MASK);
				SIVAL(pdata,2,0xFFFFFFFF);
				SIVAL(pdata,6,0xFFFFFFFF);
				break;
			case SMB_ACL_OTHER:
				SCVAL(pdata,0,SMB_POSIX_ACL_OTHER);
				SIVAL(pdata,2,0xFFFFFFFF);
				SIVAL(pdata,6,0xFFFFFFFF);
				break;
			default:
				DEBUG(0,("marshall_posix_acl: unknown tagtype.\n"));
				return False;
		}
		pdata += SMB_POSIX_ACL_ENTRY_SIZE;
	}

	return True;
}
#endif

/****************************************************************************
 Store the FILE_UNIX_BASIC info.
****************************************************************************/

static char *store_file_unix_basic(connection_struct *conn,
				char *pdata,
				files_struct *fsp,
				const SMB_STRUCT_STAT *psbuf)
{
	DEBUG(10,("store_file_unix_basic: SMB_QUERY_FILE_UNIX_BASIC\n"));
	DEBUG(4,("store_file_unix_basic: st_mode=%o\n",(int)psbuf->st_mode));

	SOFF_T(pdata,0,get_file_size_stat(psbuf));             /* File size 64 Bit */
	pdata += 8;

	SOFF_T(pdata,0,SMB_VFS_GET_ALLOC_SIZE(conn,fsp,psbuf)); /* Number of bytes used on disk - 64 Bit */
	pdata += 8;

	put_long_date_timespec(pdata,get_ctimespec(psbuf));       /* Change Time 64 Bit */
	put_long_date_timespec(pdata+8,get_atimespec(psbuf));     /* Last access time 64 Bit */
	put_long_date_timespec(pdata+16,get_mtimespec(psbuf));    /* Last modification time 64 Bit */
	pdata += 24;

	SIVAL(pdata,0,psbuf->st_uid);               /* user id for the owner */
	SIVAL(pdata,4,0);
	pdata += 8;

	SIVAL(pdata,0,psbuf->st_gid);               /* group id of owner */
	SIVAL(pdata,4,0);
	pdata += 8;

	SIVAL(pdata,0,unix_filetype(psbuf->st_mode));
	pdata += 4;

	SIVAL(pdata,0,unix_dev_major(psbuf->st_rdev));   /* Major device number if type is device */
	SIVAL(pdata,4,0);
	pdata += 8;

	SIVAL(pdata,0,unix_dev_minor(psbuf->st_rdev));   /* Minor device number if type is device */
	SIVAL(pdata,4,0);
	pdata += 8;

	SINO_T_VAL(pdata,0,(SMB_INO_T)psbuf->st_ino);   /* inode number */
	pdata += 8;
				
	SIVAL(pdata,0, unix_perms_to_wire(psbuf->st_mode));     /* Standard UNIX file permissions */
	SIVAL(pdata,4,0);
	pdata += 8;

	SIVAL(pdata,0,psbuf->st_nlink);             /* number of hard links */
	SIVAL(pdata,4,0);
	pdata += 8;

	return pdata;
}

/* Forward and reverse mappings from the UNIX_INFO2 file flags field and
 * the chflags(2) (or equivalent) flags.
 *
 * XXX: this really should be behind the VFS interface. To do this, we would
 * need to alter SMB_STRUCT_STAT so that it included a flags and a mask field.
 * Each VFS module could then implement its own mapping as appropriate for the
 * platform. We would then pass the SMB flags into SMB_VFS_CHFLAGS.
 */
static const struct {unsigned stat_fflag; unsigned smb_fflag;}
	info2_flags_map[] =
{
#ifdef UF_NODUMP
    { UF_NODUMP, EXT_DO_NOT_BACKUP },
#endif

#ifdef UF_IMMUTABLE
    { UF_IMMUTABLE, EXT_IMMUTABLE },
#endif

#ifdef UF_APPEND
    { UF_APPEND, EXT_OPEN_APPEND_ONLY },
#endif

#ifdef UF_HIDDEN
    { UF_HIDDEN, EXT_HIDDEN },
#endif

    /* Do not remove. We need to guarantee that this array has at least one
     * entry to build on HP-UX.
     */
    { 0, 0 }

};

static void map_info2_flags_from_sbuf(const SMB_STRUCT_STAT *psbuf,
				uint32 *smb_fflags, uint32 *smb_fmask)
{
#ifdef HAVE_STAT_ST_FLAGS
	int i;

	for (i = 0; i < ARRAY_SIZE(info2_flags_map); ++i) {
	    *smb_fmask |= info2_flags_map[i].smb_fflag;
	    if (psbuf->st_flags & info2_flags_map[i].stat_fflag) {
		    *smb_fflags |= info2_flags_map[i].smb_fflag;
	    }
	}
#endif /* HAVE_STAT_ST_FLAGS */
}

static bool map_info2_flags_to_sbuf(const SMB_STRUCT_STAT *psbuf,
				const uint32 smb_fflags,
				const uint32 smb_fmask,
				int *stat_fflags)
{
#ifdef HAVE_STAT_ST_FLAGS
	uint32 max_fmask = 0;
	int i;

	*stat_fflags = psbuf->st_flags;

	/* For each flags requested in smb_fmask, check the state of the
	 * corresponding flag in smb_fflags and set or clear the matching
	 * stat flag.
	 */

	for (i = 0; i < ARRAY_SIZE(info2_flags_map); ++i) {
	    max_fmask |= info2_flags_map[i].smb_fflag;
	    if (smb_fmask & info2_flags_map[i].smb_fflag) {
		    if (smb_fflags & info2_flags_map[i].smb_fflag) {
			    *stat_fflags |= info2_flags_map[i].stat_fflag;
		    } else {
			    *stat_fflags &= ~info2_flags_map[i].stat_fflag;
		    }
	    }
	}

	/* If smb_fmask is asking to set any bits that are not supported by
	 * our flag mappings, we should fail.
	 */
	if ((smb_fmask & max_fmask) != smb_fmask) {
		return False;
	}

	return True;
#else
	return False;
#endif /* HAVE_STAT_ST_FLAGS */
}


/* Just like SMB_QUERY_FILE_UNIX_BASIC, but with the addition
 * of file flags and birth (create) time.
 */
static char *store_file_unix_basic_info2(connection_struct *conn,
				char *pdata,
				files_struct *fsp,
				const SMB_STRUCT_STAT *psbuf)
{
	uint32 file_flags = 0;
	uint32 flags_mask = 0;

	pdata = store_file_unix_basic(conn, pdata, fsp, psbuf);

	/* Create (birth) time 64 bit */
	put_long_date_timespec(pdata, get_create_timespec(psbuf, False));
	pdata += 8;

	map_info2_flags_from_sbuf(psbuf, &file_flags, &flags_mask);
	SIVAL(pdata, 0, file_flags); /* flags */
	SIVAL(pdata, 4, flags_mask); /* mask */
	pdata += 8;

	return pdata;
}

static NTSTATUS marshall_stream_info(unsigned int num_streams,
				     const struct stream_struct *streams,
				     char *data,
				     unsigned int max_data_bytes,
				     unsigned int *data_size)
{
	unsigned int i;
	unsigned int ofs = 0;

	for (i = 0; i < num_streams && ofs <= max_data_bytes; i++) {
		unsigned int next_offset;
		size_t namelen;
		smb_ucs2_t *namebuf;

		if (!push_ucs2_talloc(talloc_tos(), &namebuf,
				      streams[i].name, &namelen) ||
		    namelen <= 2)
		{
			return NT_STATUS_INVALID_PARAMETER;
		}

		/*
		 * name_buf is now null-terminated, we need to marshall as not
		 * terminated
		 */

		namelen -= 2;

		SIVAL(data, ofs+4, namelen);
		SOFF_T(data, ofs+8, streams[i].size);
		SOFF_T(data, ofs+16, streams[i].alloc_size);
		memcpy(data+ofs+24, namebuf, namelen);
		TALLOC_FREE(namebuf);

		next_offset = ofs + 24 + namelen;

		if (i == num_streams-1) {
			SIVAL(data, ofs, 0);
		}
		else {
			unsigned int align = ndr_align_size(next_offset, 8);

			memset(data+next_offset, 0, align);
			next_offset += align;

			SIVAL(data, ofs, next_offset - ofs);
			ofs = next_offset;
		}

		ofs = next_offset;
	}

	*data_size = ofs;

	return NT_STATUS_OK;
}

/****************************************************************************
 Reply to a TRANSACT2_QFILEINFO on a PIPE !
****************************************************************************/

static void call_trans2qpipeinfo(connection_struct *conn,
				 struct smb_request *req,
				 unsigned int tran_call,
				 char **pparams, int total_params,
				 char **ppdata, int total_data,
				 unsigned int max_data_bytes)
{
	char *params = *pparams;
	char *pdata = *ppdata;
	unsigned int data_size = 0;
	unsigned int param_size = 2;
	uint16 info_level;
	files_struct *fsp;

	if (!params) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	if (total_params < 4) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	fsp = file_fsp(req, SVAL(params,0));
	if (!fsp_is_np(fsp)) {
		reply_nterror(req, NT_STATUS_INVALID_HANDLE);
		return;
	}

	info_level = SVAL(params,2);

	*pparams = (char *)SMB_REALLOC(*pparams,2);
	if (*pparams == NULL) {
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}
	params = *pparams;
	SSVAL(params,0,0);
	data_size = max_data_bytes + DIR_ENTRY_SAFETY_MARGIN;
	*ppdata = (char *)SMB_REALLOC(*ppdata, data_size); 
	if (*ppdata == NULL ) {
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}
	pdata = *ppdata;

	switch (info_level) {
		case SMB_FILE_STANDARD_INFORMATION:
			memset(pdata,0,24);
			SOFF_T(pdata,0,4096LL);
			SIVAL(pdata,16,1);
			SIVAL(pdata,20,1);
			data_size = 24;
			break;

		default:
			reply_nterror(req, NT_STATUS_INVALID_LEVEL);
			return;
	}

	send_trans2_replies(conn, req, params, param_size, *ppdata, data_size,
			    max_data_bytes);

	return;
}

/****************************************************************************
 Reply to a TRANS2_QFILEPATHINFO or TRANSACT2_QFILEINFO (query file info by
 file name or file id).
****************************************************************************/

static void call_trans2qfilepathinfo(connection_struct *conn,
				     struct smb_request *req,
				     unsigned int tran_call,
				     char **pparams, int total_params,
				     char **ppdata, int total_data,
				     unsigned int max_data_bytes)
{
	char *params = *pparams;
	char *pdata = *ppdata;
	char *dstart, *dend;
	uint16 info_level;
	int mode=0;
	int nlink;
	SMB_OFF_T file_size=0;
	uint64_t allocation_size=0;
	unsigned int data_size = 0;
	unsigned int param_size = 2;
	SMB_STRUCT_STAT sbuf;
	char *dos_fname = NULL;
	char *fname = NULL;
	char *fullpathname;
	char *base_name;
	char *p;
	SMB_OFF_T pos = 0;
	bool delete_pending = False;
	int len;
	time_t create_time, mtime, atime;
	struct timespec create_time_ts, mtime_ts, atime_ts;
	struct timespec write_time_ts;
	files_struct *fsp = NULL;
	struct file_id fileid;
	struct ea_list *ea_list = NULL;
	char *lock_data = NULL;
	bool ms_dfs_link = false;
	TALLOC_CTX *ctx = talloc_tos();

	if (!params) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	ZERO_STRUCT(sbuf);
	ZERO_STRUCT(write_time_ts);

	if (tran_call == TRANSACT2_QFILEINFO) {
		if (total_params < 4) {
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}

		if (IS_IPC(conn)) {
			call_trans2qpipeinfo(conn, req,	tran_call,
					     pparams, total_params,
					     ppdata, total_data,
					     max_data_bytes);
			return;
		}

		fsp = file_fsp(req, SVAL(params,0));
		info_level = SVAL(params,2);

		DEBUG(3,("call_trans2qfilepathinfo: TRANSACT2_QFILEINFO: level = %d\n", info_level));

		if (INFO_LEVEL_IS_UNIX(info_level) && !lp_unix_extensions()) {
			reply_nterror(req, NT_STATUS_INVALID_LEVEL);
			return;
		}

		/* Initial check for valid fsp ptr. */
		if (!check_fsp_open(conn, req, fsp)) {
			return;
		}

		fname = talloc_strdup(talloc_tos(),fsp->fsp_name);
		if (!fname) {
			reply_nterror(req, NT_STATUS_NO_MEMORY);
			return;
		}

		if(fsp->fake_file_handle) {
			/*
			 * This is actually for the QUOTA_FAKE_FILE --metze
			 */

			/* We know this name is ok, it's already passed the checks. */

		} else if(fsp && (fsp->is_directory || fsp->fh->fd == -1)) {
			/*
			 * This is actually a QFILEINFO on a directory
			 * handle (returned from an NT SMB). NT5.0 seems
			 * to do this call. JRA.
			 */

			if (INFO_LEVEL_IS_UNIX(info_level)) {
				/* Always do lstat for UNIX calls. */
				if (SMB_VFS_LSTAT(conn,fname,&sbuf)) {
					DEBUG(3,("call_trans2qfilepathinfo: SMB_VFS_LSTAT of %s failed (%s)\n",fname,strerror(errno)));
					reply_unixerror(req,ERRDOS,ERRbadpath);
					return;
				}
			} else if (SMB_VFS_STAT(conn,fname,&sbuf)) {
				DEBUG(3,("call_trans2qfilepathinfo: SMB_VFS_STAT of %s failed (%s)\n",fname,strerror(errno)));
				reply_unixerror(req, ERRDOS, ERRbadpath);
				return;
			}

			fileid = vfs_file_id_from_sbuf(conn, &sbuf);
			get_file_infos(fileid, &delete_pending, &write_time_ts);
		} else {
			/*
			 * Original code - this is an open file.
			 */
			if (!check_fsp(conn, req, fsp)) {
				return;
			}

			if (SMB_VFS_FSTAT(fsp, &sbuf) != 0) {
				DEBUG(3,("fstat of fnum %d failed (%s)\n", fsp->fnum, strerror(errno)));
				reply_unixerror(req, ERRDOS, ERRbadfid);
				return;
			}
			pos = fsp->fh->position_information;
			fileid = vfs_file_id_from_sbuf(conn, &sbuf);
			get_file_infos(fileid, &delete_pending, &write_time_ts);
		}

	} else {
		NTSTATUS status = NT_STATUS_OK;

		/* qpathinfo */
		if (total_params < 7) {
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}

		info_level = SVAL(params,0);

		DEBUG(3,("call_trans2qfilepathinfo: TRANSACT2_QPATHINFO: level = %d\n", info_level));

		if (INFO_LEVEL_IS_UNIX(info_level) && !lp_unix_extensions()) {
			reply_nterror(req, NT_STATUS_INVALID_LEVEL);
			return;
		}

		srvstr_get_path(ctx, params, req->flags2, &fname, &params[6],
				total_params - 6,
				STR_TERMINATE, &status);
		if (!NT_STATUS_IS_OK(status)) {
			reply_nterror(req, status);
			return;
		}

		status = resolve_dfspath(ctx,
					conn,
					req->flags2 & FLAGS2_DFS_PATHNAMES,
					fname,
					&fname);
		if (!NT_STATUS_IS_OK(status)) {
			if (NT_STATUS_EQUAL(status,NT_STATUS_PATH_NOT_COVERED)) {
				reply_botherror(req,
						NT_STATUS_PATH_NOT_COVERED,
						ERRSRV, ERRbadpath);
			}
			reply_nterror(req, status);
			return;
		}

		status = unix_convert(ctx, conn, fname, False, &fname, NULL, &sbuf);
		if (!NT_STATUS_IS_OK(status)) {
			reply_nterror(req, status);
			return;
		}
		status = check_name(conn, fname);
		if (!NT_STATUS_IS_OK(status)) {
			DEBUG(3,("call_trans2qfilepathinfo: fileinfo of %s failed (%s)\n",fname,nt_errstr(status)));
			reply_nterror(req, status);
			return;
		}

		if ((conn->fs_capabilities & FILE_NAMED_STREAMS)
		    && is_ntfs_stream_name(fname)) {
			char *base;
			SMB_STRUCT_STAT bsbuf;

			status = split_ntfs_stream_name(talloc_tos(), fname,
							&base, NULL);
			if (!NT_STATUS_IS_OK(status)) {
				DEBUG(10, ("create_file_unixpath: "
					"split_ntfs_stream_name failed: %s\n",
					nt_errstr(status)));
				reply_nterror(req, status);
				return;
			}

			SMB_ASSERT(!is_ntfs_stream_name(base));	/* paranoia.. */

			if (INFO_LEVEL_IS_UNIX(info_level)) {
				/* Always do lstat for UNIX calls. */
				if (SMB_VFS_LSTAT(conn,base,&bsbuf)) {
					DEBUG(3,("call_trans2qfilepathinfo: SMB_VFS_LSTAT of %s failed (%s)\n",base,strerror(errno)));
					reply_unixerror(req,ERRDOS,ERRbadpath);
					return;
				}
			} else {
				if (SMB_VFS_STAT(conn,base,&bsbuf) != 0) {
					DEBUG(3,("call_trans2qfilepathinfo: fileinfo of %s failed (%s)\n",base,strerror(errno)));
					reply_unixerror(req,ERRDOS,ERRbadpath);
					return;
				}
			}

			fileid = vfs_file_id_from_sbuf(conn, &bsbuf);
			get_file_infos(fileid, &delete_pending, NULL);
			if (delete_pending) {
				reply_nterror(req, NT_STATUS_DELETE_PENDING);
				return;
			}
		}

		if (INFO_LEVEL_IS_UNIX(info_level)) {
			/* Always do lstat for UNIX calls. */
			if (SMB_VFS_LSTAT(conn,fname,&sbuf)) {
				DEBUG(3,("call_trans2qfilepathinfo: SMB_VFS_LSTAT of %s failed (%s)\n",fname,strerror(errno)));
				reply_unixerror(req, ERRDOS, ERRbadpath);
				return;
			}

		} else if (!VALID_STAT(sbuf) && SMB_VFS_STAT(conn,fname,&sbuf) && (info_level != SMB_INFO_IS_NAME_VALID)) {
			ms_dfs_link = check_msdfs_link(conn,fname,&sbuf);

			if (!ms_dfs_link) {
				DEBUG(3,("call_trans2qfilepathinfo: SMB_VFS_STAT of %s failed (%s)\n",fname,strerror(errno)));
				reply_unixerror(req, ERRDOS, ERRbadpath);
				return;
			}
		}

		fileid = vfs_file_id_from_sbuf(conn, &sbuf);
		get_file_infos(fileid, &delete_pending, &write_time_ts);
		if (delete_pending) {
			reply_nterror(req, NT_STATUS_DELETE_PENDING);
			return;
		}
	}

	if (INFO_LEVEL_IS_UNIX(info_level) && !lp_unix_extensions()) {
		reply_nterror(req, NT_STATUS_INVALID_LEVEL);
		return;
	}

	DEBUG(3,("call_trans2qfilepathinfo %s (fnum = %d) level=%d call=%d total_data=%d\n",
		fname,fsp ? fsp->fnum : -1, info_level,tran_call,total_data));

	p = strrchr_m(fname,'/');
	if (!p)
		base_name = fname;
	else
		base_name = p+1;

	if (ms_dfs_link) {
		mode = dos_mode_msdfs(conn,fname,&sbuf);
	} else {
		mode = dos_mode(conn,fname,&sbuf);
	}
	if (!mode)
		mode = FILE_ATTRIBUTE_NORMAL;

	nlink = sbuf.st_nlink;

	if (nlink && (mode&aDIR)) {
		nlink = 1;
	}

	if ((nlink > 0) && delete_pending) {
		nlink -= 1;
	}

	fullpathname = fname;
	if (!(mode & aDIR))
		file_size = get_file_size_stat(&sbuf);

	/* Pull out any data sent here before we realloc. */
	switch (info_level) {
		case SMB_INFO_QUERY_EAS_FROM_LIST:
		{
			/* Pull any EA list from the data portion. */
			uint32 ea_size;

			if (total_data < 4) {
				reply_nterror(
					req, NT_STATUS_INVALID_PARAMETER);
				return;
			}
			ea_size = IVAL(pdata,0);

			if (total_data > 0 && ea_size != total_data) {
				DEBUG(4,("call_trans2qfilepathinfo: Rejecting EA request with incorrect \
total_data=%u (should be %u)\n", (unsigned int)total_data, (unsigned int)IVAL(pdata,0) ));
				reply_nterror(
					req, NT_STATUS_INVALID_PARAMETER);
				return;
			}

			if (!lp_ea_support(SNUM(conn))) {
				reply_doserror(req, ERRDOS,
					       ERReasnotsupported);
				return;
			}

			/* Pull out the list of names. */
			ea_list = read_ea_name_list(ctx, pdata + 4, ea_size - 4);
			if (!ea_list) {
				reply_nterror(
					req, NT_STATUS_INVALID_PARAMETER);
				return;
			}
			break;
		}

		case SMB_QUERY_POSIX_LOCK:
		{
			if (fsp == NULL || fsp->fh->fd == -1) {
				reply_nterror(req, NT_STATUS_INVALID_HANDLE);
				return;
			}

			if (total_data != POSIX_LOCK_DATA_SIZE) {
				reply_nterror(
					req, NT_STATUS_INVALID_PARAMETER);
				return;
			}

			/* Copy the lock range data. */
			lock_data = (char *)TALLOC_MEMDUP(
				ctx, pdata, total_data);
			if (!lock_data) {
				reply_nterror(req, NT_STATUS_NO_MEMORY);
				return;
			}
		}
		default:
			break;
	}

	*pparams = (char *)SMB_REALLOC(*pparams,2);
	if (*pparams == NULL) {
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}
	params = *pparams;
	SSVAL(params,0,0);
	data_size = max_data_bytes + DIR_ENTRY_SAFETY_MARGIN;
	*ppdata = (char *)SMB_REALLOC(*ppdata, data_size); 
	if (*ppdata == NULL ) {
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}
	pdata = *ppdata;
	dstart = pdata;
	dend = dstart + data_size - 1;

	create_time_ts = get_create_timespec(&sbuf,lp_fake_dir_create_times(SNUM(conn)));
	mtime_ts = get_mtimespec(&sbuf);
	atime_ts = get_atimespec(&sbuf);

	allocation_size = SMB_VFS_GET_ALLOC_SIZE(conn,fsp,&sbuf);

	if (!fsp) {
		/* Do we have this path open ? */
		files_struct *fsp1;
		fileid = vfs_file_id_from_sbuf(conn, &sbuf);
		fsp1 = file_find_di_first(fileid);
		if (fsp1 && fsp1->initial_allocation_size) {
			allocation_size = SMB_VFS_GET_ALLOC_SIZE(conn, fsp1, &sbuf);
		}
	}

	if (!null_timespec(write_time_ts) && !INFO_LEVEL_IS_UNIX(info_level)) {
		mtime_ts = write_time_ts;
	}

	if (lp_dos_filetime_resolution(SNUM(conn))) {
		dos_filetime_timespec(&create_time_ts);
		dos_filetime_timespec(&mtime_ts);
		dos_filetime_timespec(&atime_ts);
	}

	create_time = convert_timespec_to_time_t(create_time_ts);
	mtime = convert_timespec_to_time_t(mtime_ts);
	atime = convert_timespec_to_time_t(atime_ts);

	/* NT expects the name to be in an exact form of the *full*
	   filename. See the trans2 torture test */
	if (ISDOT(base_name)) {
		dos_fname = talloc_strdup(ctx, "\\");
		if (!dos_fname) {
			reply_nterror(req, NT_STATUS_NO_MEMORY);
			return;
		}
	} else {
		dos_fname = talloc_asprintf(ctx,
				"\\%s",
				fname);
		if (!dos_fname) {
			reply_nterror(req, NT_STATUS_NO_MEMORY);
			return;
		}
		string_replace(dos_fname, '/', '\\');
	}

	switch (info_level) {
		case SMB_INFO_STANDARD:
			DEBUG(10,("call_trans2qfilepathinfo: SMB_INFO_STANDARD\n"));
			data_size = 22;
			srv_put_dos_date2(pdata,l1_fdateCreation,create_time);
			srv_put_dos_date2(pdata,l1_fdateLastAccess,atime);
			srv_put_dos_date2(pdata,l1_fdateLastWrite,mtime); /* write time */
			SIVAL(pdata,l1_cbFile,(uint32)file_size);
			SIVAL(pdata,l1_cbFileAlloc,(uint32)allocation_size);
			SSVAL(pdata,l1_attrFile,mode);
			break;

		case SMB_INFO_QUERY_EA_SIZE:
		{
			unsigned int ea_size = estimate_ea_size(conn, fsp, fname);
			DEBUG(10,("call_trans2qfilepathinfo: SMB_INFO_QUERY_EA_SIZE\n"));
			data_size = 26;
			srv_put_dos_date2(pdata,0,create_time);
			srv_put_dos_date2(pdata,4,atime);
			srv_put_dos_date2(pdata,8,mtime); /* write time */
			SIVAL(pdata,12,(uint32)file_size);
			SIVAL(pdata,16,(uint32)allocation_size);
			SSVAL(pdata,20,mode);
			SIVAL(pdata,22,ea_size);
			break;
		}

		case SMB_INFO_IS_NAME_VALID:
			DEBUG(10,("call_trans2qfilepathinfo: SMB_INFO_IS_NAME_VALID\n"));
			if (tran_call == TRANSACT2_QFILEINFO) {
				/* os/2 needs this ? really ?*/
				reply_doserror(req, ERRDOS, ERRbadfunc);
				return;
			}
			data_size = 0;
			param_size = 0;
			break;

		case SMB_INFO_QUERY_EAS_FROM_LIST:
		{
			size_t total_ea_len = 0;
			struct ea_list *ea_file_list = NULL;

			DEBUG(10,("call_trans2qfilepathinfo: SMB_INFO_QUERY_EAS_FROM_LIST\n"));

			ea_file_list = get_ea_list_from_file(ctx, conn, fsp, fname, &total_ea_len);
			ea_list = ea_list_union(ea_list, ea_file_list, &total_ea_len);

			if (!ea_list || (total_ea_len > data_size)) {
				data_size = 4;
				SIVAL(pdata,0,4);   /* EA List Length must be set to 4 if no EA's. */
				break;
			}

			data_size = fill_ea_buffer(ctx, pdata, data_size, conn, ea_list);
			break;
		}

		case SMB_INFO_QUERY_ALL_EAS:
		{
			/* We have data_size bytes to put EA's into. */
			size_t total_ea_len = 0;

			DEBUG(10,("call_trans2qfilepathinfo: SMB_INFO_QUERY_ALL_EAS\n"));

			ea_list = get_ea_list_from_file(ctx, conn, fsp, fname, &total_ea_len);
			if (!ea_list || (total_ea_len > data_size)) {
				data_size = 4;
				SIVAL(pdata,0,4);   /* EA List Length must be set to 4 if no EA's. */
				break;
			}

			data_size = fill_ea_buffer(ctx, pdata, data_size, conn, ea_list);
			break;
		}

		case SMB_FILE_BASIC_INFORMATION:
		case SMB_QUERY_FILE_BASIC_INFO:

			if (info_level == SMB_QUERY_FILE_BASIC_INFO) {
				DEBUG(10,("call_trans2qfilepathinfo: SMB_QUERY_FILE_BASIC_INFO\n"));
				data_size = 36; /* w95 returns 40 bytes not 36 - why ?. */
			} else {
				DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_BASIC_INFORMATION\n"));
				data_size = 40;
				SIVAL(pdata,36,0);
			}
			put_long_date_timespec(pdata,create_time_ts);
			put_long_date_timespec(pdata+8,atime_ts);
			put_long_date_timespec(pdata+16,mtime_ts); /* write time */
			put_long_date_timespec(pdata+24,mtime_ts); /* change time */
			SIVAL(pdata,32,mode);

			DEBUG(5,("SMB_QFBI - "));
			DEBUG(5,("create: %s ", ctime(&create_time)));
			DEBUG(5,("access: %s ", ctime(&atime)));
			DEBUG(5,("write: %s ", ctime(&mtime)));
			DEBUG(5,("change: %s ", ctime(&mtime)));
			DEBUG(5,("mode: %x\n", mode));
			break;

		case SMB_FILE_STANDARD_INFORMATION:
		case SMB_QUERY_FILE_STANDARD_INFO:

			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_STANDARD_INFORMATION\n"));
			data_size = 24;
			SOFF_T(pdata,0,allocation_size);
			SOFF_T(pdata,8,file_size);
			SIVAL(pdata,16,nlink);
			SCVAL(pdata,20,delete_pending?1:0);
			SCVAL(pdata,21,(mode&aDIR)?1:0);
			SSVAL(pdata,22,0); /* Padding. */
			break;

		case SMB_FILE_EA_INFORMATION:
		case SMB_QUERY_FILE_EA_INFO:
		{
			unsigned int ea_size = estimate_ea_size(conn, fsp, fname);
			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_EA_INFORMATION\n"));
			data_size = 4;
			SIVAL(pdata,0,ea_size);
			break;
		}

		/* Get the 8.3 name - used if NT SMB was negotiated. */
		case SMB_QUERY_FILE_ALT_NAME_INFO:
		case SMB_FILE_ALTERNATE_NAME_INFORMATION:
		{
			char mangled_name[13];
			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_ALTERNATE_NAME_INFORMATION\n"));
			if (!name_to_8_3(base_name,mangled_name,
						True,conn->params)) {
				reply_nterror(
					req,
					NT_STATUS_NO_MEMORY);
			}
			len = srvstr_push(dstart, req->flags2,
					  pdata+4, mangled_name,
					  PTR_DIFF(dend, pdata+4),
					  STR_UNICODE);
			data_size = 4 + len;
			SIVAL(pdata,0,len);
			break;
		}

		case SMB_QUERY_FILE_NAME_INFO:
			/*
			  this must be *exactly* right for ACLs on mapped drives to work
			 */
			len = srvstr_push(dstart, req->flags2,
					  pdata+4, dos_fname,
					  PTR_DIFF(dend, pdata+4),
					  STR_UNICODE);
			DEBUG(10,("call_trans2qfilepathinfo: SMB_QUERY_FILE_NAME_INFO\n"));
			data_size = 4 + len;
			SIVAL(pdata,0,len);
			break;

		case SMB_FILE_ALLOCATION_INFORMATION:
		case SMB_QUERY_FILE_ALLOCATION_INFO:
			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_ALLOCATION_INFORMATION\n"));
			data_size = 8;
			SOFF_T(pdata,0,allocation_size);
			break;

		case SMB_FILE_END_OF_FILE_INFORMATION:
		case SMB_QUERY_FILE_END_OF_FILEINFO:
			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_END_OF_FILE_INFORMATION\n"));
			data_size = 8;
			SOFF_T(pdata,0,file_size);
			break;

		case SMB_QUERY_FILE_ALL_INFO:
		case SMB_FILE_ALL_INFORMATION:
		{
			unsigned int ea_size = estimate_ea_size(conn, fsp, fname);
			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_ALL_INFORMATION\n"));
			put_long_date_timespec(pdata,create_time_ts);
			put_long_date_timespec(pdata+8,atime_ts);
			put_long_date_timespec(pdata+16,mtime_ts); /* write time */
			put_long_date_timespec(pdata+24,mtime_ts); /* change time */
			SIVAL(pdata,32,mode);
			SIVAL(pdata,36,0); /* padding. */
			pdata += 40;
			SOFF_T(pdata,0,allocation_size);
			SOFF_T(pdata,8,file_size);
			SIVAL(pdata,16,nlink);
			SCVAL(pdata,20,delete_pending);
			SCVAL(pdata,21,(mode&aDIR)?1:0);
			SSVAL(pdata,22,0);
			pdata += 24;
			SIVAL(pdata,0,ea_size);
			pdata += 4; /* EA info */
			len = srvstr_push(dstart, req->flags2,
					  pdata+4, dos_fname,
					  PTR_DIFF(dend, pdata+4),
					  STR_UNICODE);
			SIVAL(pdata,0,len);
			pdata += 4 + len;
			data_size = PTR_DIFF(pdata,(*ppdata));
			break;
		}
		case SMB_FILE_INTERNAL_INFORMATION:
			/* This should be an index number - looks like
			   dev/ino to me :-) 

			   I think this causes us to fail the IFSKIT
			   BasicFileInformationTest. -tpot */

			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_INTERNAL_INFORMATION\n"));
			SIVAL(pdata,0,sbuf.st_ino); /* FileIndexLow */
			SIVAL(pdata,4,sbuf.st_dev); /* FileIndexHigh */
			data_size = 8;
			break;

		case SMB_FILE_ACCESS_INFORMATION:
			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_ACCESS_INFORMATION\n"));
			if (fsp) {
				SIVAL(pdata,0,fsp->access_mask);
			} else {
				/* GENERIC_EXECUTE mapping from Windows */
				SIVAL(pdata,0,0x12019F);
			}
			data_size = 4;
			break;

		case SMB_FILE_NAME_INFORMATION:
			/* Pathname with leading '\'. */
			{
				size_t byte_len;
				byte_len = dos_PutUniCode(pdata+4,dos_fname,(size_t)max_data_bytes,False);
				DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_NAME_INFORMATION\n"));
				SIVAL(pdata,0,byte_len);
				data_size = 4 + byte_len;
				break;
			}

		case SMB_FILE_DISPOSITION_INFORMATION:
			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_DISPOSITION_INFORMATION\n"));
			data_size = 1;
			SCVAL(pdata,0,delete_pending);
			break;

		case SMB_FILE_POSITION_INFORMATION:
			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_POSITION_INFORMATION\n"));
			data_size = 8;
			SOFF_T(pdata,0,pos);
			break;

		case SMB_FILE_MODE_INFORMATION:
			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_MODE_INFORMATION\n"));
			SIVAL(pdata,0,mode);
			data_size = 4;
			break;

		case SMB_FILE_ALIGNMENT_INFORMATION:
			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_ALIGNMENT_INFORMATION\n"));
			SIVAL(pdata,0,0); /* No alignment needed. */
			data_size = 4;
			break;

		/*
		 * NT4 server just returns "invalid query" to this - if we try
		 * to answer it then NTws gets a BSOD! (tridge).  W2K seems to
		 * want this. JRA.
		 */
		/* The first statement above is false - verified using Thursby
		 * client against NT4 -- gcolley.
		 */
		case SMB_QUERY_FILE_STREAM_INFO:
		case SMB_FILE_STREAM_INFORMATION: {
			unsigned int num_streams;
			struct stream_struct *streams;
			NTSTATUS status;

			DEBUG(10,("call_trans2qfilepathinfo: "
				  "SMB_FILE_STREAM_INFORMATION\n"));

			status = SMB_VFS_STREAMINFO(
				conn, fsp, fname, talloc_tos(),
				&num_streams, &streams);

			if (!NT_STATUS_IS_OK(status)) {
				DEBUG(10, ("could not get stream info: %s\n",
					   nt_errstr(status)));
				reply_nterror(req, status);
				return;
			}

			status = marshall_stream_info(num_streams, streams,
						      pdata, max_data_bytes,
						      &data_size);

			if (!NT_STATUS_IS_OK(status)) {
				DEBUG(10, ("marshall_stream_info failed: %s\n",
					   nt_errstr(status)));
				reply_nterror(req, status);
				return;
			}

			TALLOC_FREE(streams);

			break;
		}
		case SMB_QUERY_COMPRESSION_INFO:
		case SMB_FILE_COMPRESSION_INFORMATION:
			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_COMPRESSION_INFORMATION\n"));
			SOFF_T(pdata,0,file_size);
			SIVAL(pdata,8,0); /* ??? */
			SIVAL(pdata,12,0); /* ??? */
			data_size = 16;
			break;

		case SMB_FILE_NETWORK_OPEN_INFORMATION:
			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_NETWORK_OPEN_INFORMATION\n"));
			put_long_date_timespec(pdata,create_time_ts);
			put_long_date_timespec(pdata+8,atime_ts);
			put_long_date_timespec(pdata+16,mtime_ts); /* write time */
			put_long_date_timespec(pdata+24,mtime_ts); /* change time */
			SOFF_T(pdata,32,allocation_size);
			SOFF_T(pdata,40,file_size);
			SIVAL(pdata,48,mode);
			SIVAL(pdata,52,0); /* ??? */
			data_size = 56;
			break;

		case SMB_FILE_ATTRIBUTE_TAG_INFORMATION:
			DEBUG(10,("call_trans2qfilepathinfo: SMB_FILE_ATTRIBUTE_TAG_INFORMATION\n"));
			SIVAL(pdata,0,mode);
			SIVAL(pdata,4,0);
			data_size = 8;
			break;

		/*
		 * CIFS UNIX Extensions.
		 */

		case SMB_QUERY_FILE_UNIX_BASIC:

			pdata = store_file_unix_basic(conn, pdata, fsp, &sbuf);
			data_size = PTR_DIFF(pdata,(*ppdata));

			{
				int i;
				DEBUG(4,("call_trans2qfilepathinfo: SMB_QUERY_FILE_UNIX_BASIC "));

				for (i=0; i<100; i++)
					DEBUG(4,("%d=%x, ",i, (*ppdata)[i]));
				DEBUG(4,("\n"));
			}

			break;

		case SMB_QUERY_FILE_UNIX_INFO2:

			pdata = store_file_unix_basic_info2(conn, pdata, fsp, &sbuf);
			data_size = PTR_DIFF(pdata,(*ppdata));

			{
				int i;
				DEBUG(4,("call_trans2qfilepathinfo: SMB_QUERY_FILE_UNIX_INFO2 "));

				for (i=0; i<100; i++)
					DEBUG(4,("%d=%x, ",i, (*ppdata)[i]));
				DEBUG(4,("\n"));
			}

			break;

		case SMB_QUERY_FILE_UNIX_LINK:
			{
				char *buffer = TALLOC_ARRAY(ctx, char, PATH_MAX+1);

				if (!buffer) {
					reply_nterror(req, NT_STATUS_NO_MEMORY);
					return;
				}

				DEBUG(10,("call_trans2qfilepathinfo: SMB_QUERY_FILE_UNIX_LINK\n"));
#ifdef S_ISLNK
				if(!S_ISLNK(sbuf.st_mode)) {
					reply_unixerror(req, ERRSRV,
							ERRbadlink);
					return;
				}
#else
				reply_unixerror(req, ERRDOS, ERRbadlink);
				return;
#endif
				len = SMB_VFS_READLINK(conn,fullpathname,
						buffer, PATH_MAX);
				if (len == -1) {
					reply_unixerror(req, ERRDOS,
							ERRnoaccess);
					return;
				}
				buffer[len] = 0;
				len = srvstr_push(dstart, req->flags2,
						  pdata, buffer,
						  PTR_DIFF(dend, pdata),
						  STR_TERMINATE);
				pdata += len;
				data_size = PTR_DIFF(pdata,(*ppdata));

				break;
			}

#if defined(HAVE_POSIX_ACLS)
		case SMB_QUERY_POSIX_ACL:
			{
				SMB_ACL_T file_acl = NULL;
				SMB_ACL_T def_acl = NULL;
				uint16 num_file_acls = 0;
				uint16 num_def_acls = 0;

				if (fsp && !fsp->is_directory && (fsp->fh->fd != -1)) {
					file_acl = SMB_VFS_SYS_ACL_GET_FD(fsp);
				} else {
					file_acl = SMB_VFS_SYS_ACL_GET_FILE(conn, fname, SMB_ACL_TYPE_ACCESS);
				}

				if (file_acl == NULL && no_acl_syscall_error(errno)) {
					DEBUG(5,("call_trans2qfilepathinfo: ACLs not implemented on filesystem containing %s\n",
						fname ));
					reply_nterror(
						req,
						NT_STATUS_NOT_IMPLEMENTED);
					return;
				}

				if (S_ISDIR(sbuf.st_mode)) {
					if (fsp && fsp->is_directory) {
						def_acl = SMB_VFS_SYS_ACL_GET_FILE(conn, fsp->fsp_name, SMB_ACL_TYPE_DEFAULT);
					} else {
						def_acl = SMB_VFS_SYS_ACL_GET_FILE(conn, fname, SMB_ACL_TYPE_DEFAULT);
					}
					def_acl = free_empty_sys_acl(conn, def_acl);
				}

				num_file_acls = count_acl_entries(conn, file_acl);
				num_def_acls = count_acl_entries(conn, def_acl);

				if ( data_size < (num_file_acls + num_def_acls)*SMB_POSIX_ACL_ENTRY_SIZE + SMB_POSIX_ACL_HEADER_SIZE) {
					DEBUG(5,("call_trans2qfilepathinfo: data_size too small (%u) need %u\n",
						data_size,
						(unsigned int)((num_file_acls + num_def_acls)*SMB_POSIX_ACL_ENTRY_SIZE +
							SMB_POSIX_ACL_HEADER_SIZE) ));
					if (file_acl) {
						SMB_VFS_SYS_ACL_FREE_ACL(conn, file_acl);
					}
					if (def_acl) {
						SMB_VFS_SYS_ACL_FREE_ACL(conn, def_acl);
					}
					reply_nterror(
						req,
						NT_STATUS_BUFFER_TOO_SMALL);
					return;
				}

				SSVAL(pdata,0,SMB_POSIX_ACL_VERSION);
				SSVAL(pdata,2,num_file_acls);
				SSVAL(pdata,4,num_def_acls);
				if (!marshall_posix_acl(conn, pdata + SMB_POSIX_ACL_HEADER_SIZE, &sbuf, file_acl)) {
					if (file_acl) {
						SMB_VFS_SYS_ACL_FREE_ACL(conn, file_acl);
					}
					if (def_acl) {
						SMB_VFS_SYS_ACL_FREE_ACL(conn, def_acl);
					}
					reply_nterror(
						req, NT_STATUS_INTERNAL_ERROR);
					return;
				}
				if (!marshall_posix_acl(conn, pdata + SMB_POSIX_ACL_HEADER_SIZE + (num_file_acls*SMB_POSIX_ACL_ENTRY_SIZE), &sbuf, def_acl)) {
					if (file_acl) {
						SMB_VFS_SYS_ACL_FREE_ACL(conn, file_acl);
					}
					if (def_acl) {
						SMB_VFS_SYS_ACL_FREE_ACL(conn, def_acl);
					}
					reply_nterror(
						req,
						NT_STATUS_INTERNAL_ERROR);
					return;
				}

				if (file_acl) {
					SMB_VFS_SYS_ACL_FREE_ACL(conn, file_acl);
				}
				if (def_acl) {
					SMB_VFS_SYS_ACL_FREE_ACL(conn, def_acl);
				}
				data_size = (num_file_acls + num_def_acls)*SMB_POSIX_ACL_ENTRY_SIZE + SMB_POSIX_ACL_HEADER_SIZE;
				break;
			}
#endif


		case SMB_QUERY_POSIX_LOCK:
		{
			NTSTATUS status = NT_STATUS_INVALID_LEVEL;
			uint64_t count;
			uint64_t offset;
			uint32 lock_pid;
			enum brl_type lock_type;

			if (total_data != POSIX_LOCK_DATA_SIZE) {
				reply_nterror(
					req, NT_STATUS_INVALID_PARAMETER);
				return;
			}

			switch (SVAL(pdata, POSIX_LOCK_TYPE_OFFSET)) {
				case POSIX_LOCK_TYPE_READ:
					lock_type = READ_LOCK;
					break;
				case POSIX_LOCK_TYPE_WRITE:
					lock_type = WRITE_LOCK;
					break;
				case POSIX_LOCK_TYPE_UNLOCK:
				default:
					/* There's no point in asking for an unlock... */
					reply_nterror(
						req,
						NT_STATUS_INVALID_PARAMETER);
					return;
			}

			lock_pid = IVAL(pdata, POSIX_LOCK_PID_OFFSET);
#if defined(HAVE_LONGLONG)
			offset = (((uint64_t) IVAL(pdata,(POSIX_LOCK_START_OFFSET+4))) << 32) |
					((uint64_t) IVAL(pdata,POSIX_LOCK_START_OFFSET));
			count = (((uint64_t) IVAL(pdata,(POSIX_LOCK_LEN_OFFSET+4))) << 32) |
					((uint64_t) IVAL(pdata,POSIX_LOCK_LEN_OFFSET));
#else /* HAVE_LONGLONG */
			offset = (uint64_t)IVAL(pdata,POSIX_LOCK_START_OFFSET);
			count = (uint64_t)IVAL(pdata,POSIX_LOCK_LEN_OFFSET);
#endif /* HAVE_LONGLONG */

			status = query_lock(fsp,
					&lock_pid,
					&count,
					&offset,
					&lock_type,
					POSIX_LOCK);

			if (ERROR_WAS_LOCK_DENIED(status)) {
				/* Here we need to report who has it locked... */
				data_size = POSIX_LOCK_DATA_SIZE;

				SSVAL(pdata, POSIX_LOCK_TYPE_OFFSET, lock_type);
				SSVAL(pdata, POSIX_LOCK_FLAGS_OFFSET, 0);
				SIVAL(pdata, POSIX_LOCK_PID_OFFSET, lock_pid);
#if defined(HAVE_LONGLONG)
				SIVAL(pdata, POSIX_LOCK_START_OFFSET, (uint32)(offset & 0xFFFFFFFF));
				SIVAL(pdata, POSIX_LOCK_START_OFFSET + 4, (uint32)((offset >> 32) & 0xFFFFFFFF));
				SIVAL(pdata, POSIX_LOCK_LEN_OFFSET, (uint32)(count & 0xFFFFFFFF));
				SIVAL(pdata, POSIX_LOCK_LEN_OFFSET + 4, (uint32)((count >> 32) & 0xFFFFFFFF));
#else /* HAVE_LONGLONG */
				SIVAL(pdata, POSIX_LOCK_START_OFFSET, offset);
				SIVAL(pdata, POSIX_LOCK_LEN_OFFSET, count);
#endif /* HAVE_LONGLONG */

			} else if (NT_STATUS_IS_OK(status)) {
				/* For success we just return a copy of what we sent
				   with the lock type set to POSIX_LOCK_TYPE_UNLOCK. */
				data_size = POSIX_LOCK_DATA_SIZE;
				memcpy(pdata, lock_data, POSIX_LOCK_DATA_SIZE);
				SSVAL(pdata, POSIX_LOCK_TYPE_OFFSET, POSIX_LOCK_TYPE_UNLOCK);
			} else {
				reply_nterror(req, status);
				return;
			}
			break;
		}

		default:
			reply_nterror(req, NT_STATUS_INVALID_LEVEL);
			return;
	}

	send_trans2_replies(conn, req, params, param_size, *ppdata, data_size,
			    max_data_bytes);

	return;
}

/****************************************************************************
 Set a hard link (called by UNIX extensions and by NT rename with HARD link
 code.
****************************************************************************/

NTSTATUS hardlink_internals(TALLOC_CTX *ctx,
		connection_struct *conn,
		const char *oldname_in,
		const char *newname_in)
{
	SMB_STRUCT_STAT sbuf1, sbuf2;
	char *last_component_oldname = NULL;
	char *last_component_newname = NULL;
	char *oldname = NULL;
	char *newname = NULL;
	NTSTATUS status = NT_STATUS_OK;

	ZERO_STRUCT(sbuf1);
	ZERO_STRUCT(sbuf2);

	status = unix_convert(ctx, conn, oldname_in, False, &oldname,
			&last_component_oldname, &sbuf1);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	status = check_name(conn, oldname);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/* source must already exist. */
	if (!VALID_STAT(sbuf1)) {
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	status = unix_convert(ctx, conn, newname_in, False, &newname,
			&last_component_newname, &sbuf2);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	status = check_name(conn, newname);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/* Disallow if newname already exists. */
	if (VALID_STAT(sbuf2)) {
		return NT_STATUS_OBJECT_NAME_COLLISION;
	}

	/* No links from a directory. */
	if (S_ISDIR(sbuf1.st_mode)) {
		return NT_STATUS_FILE_IS_A_DIRECTORY;
	}

	/* Ensure this is within the share. */
	status = check_reduced_name(conn, oldname);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	DEBUG(10,("hardlink_internals: doing hard link %s -> %s\n", newname, oldname ));

	if (SMB_VFS_LINK(conn,oldname,newname) != 0) {
		status = map_nt_error_from_unix(errno);
		DEBUG(3,("hardlink_internals: Error %s hard link %s -> %s\n",
                                nt_errstr(status), newname, oldname));
	}

	return status;
}

/****************************************************************************
 Deal with setting the time from any of the setfilepathinfo functions.
****************************************************************************/

NTSTATUS smb_set_file_time(connection_struct *conn,
			   files_struct *fsp,
			   const char *fname,
			   const SMB_STRUCT_STAT *psbuf,
			   struct smb_file_time *ft,
			   bool setting_write_time)
{
	uint32 action =
		FILE_NOTIFY_CHANGE_LAST_ACCESS
		|FILE_NOTIFY_CHANGE_LAST_WRITE;

	if (!VALID_STAT(*psbuf)) {
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	/* get some defaults (no modifications) if any info is zero or -1. */
	if (null_timespec(ft->atime)) {
		ft->atime= get_atimespec(psbuf);
		action &= ~FILE_NOTIFY_CHANGE_LAST_ACCESS;
	}

	if (null_timespec(ft->mtime)) {
		ft->mtime = get_mtimespec(psbuf);
		action &= ~FILE_NOTIFY_CHANGE_LAST_WRITE;
	}

	if (!setting_write_time) {
		/* ft->mtime comes from change time, not write time. */
		action &= ~FILE_NOTIFY_CHANGE_LAST_WRITE;
	}

	DEBUG(5,("smb_set_filetime: actime: %s\n ",
		time_to_asc(convert_timespec_to_time_t(ft->atime))));
	DEBUG(5,("smb_set_filetime: modtime: %s\n ",
		time_to_asc(convert_timespec_to_time_t(ft->mtime))));
	if (!null_timespec(ft->create_time)) {
		DEBUG(5,("smb_set_file_time: createtime: %s\n ",
		   time_to_asc(convert_timespec_to_time_t(ft->create_time))));
	}

	/*
	 * Try and set the times of this file if
	 * they are different from the current values.
	 */

	{
		struct timespec mts = get_mtimespec(psbuf);
		struct timespec ats = get_atimespec(psbuf);
		if ((timespec_compare(&ft->atime, &ats) == 0) &&
		    (timespec_compare(&ft->mtime, &mts) == 0)) {
			return NT_STATUS_OK;
		}
	}

	if (setting_write_time) {
		/*
		 * This was a Windows setfileinfo on an open file.
		 * NT does this a lot. We also need to 
		 * set the time here, as it can be read by 
		 * FindFirst/FindNext and with the patch for bug #2045
		 * in smbd/fileio.c it ensures that this timestamp is
		 * kept sticky even after a write. We save the request
		 * away and will set it on file close and after a write. JRA.
		 */

		DEBUG(10,("smb_set_file_time: setting pending modtime to %s\n",
			  time_to_asc(convert_timespec_to_time_t(ft->mtime))));

		if (fsp != NULL) {
			if (fsp->base_fsp) {
				set_sticky_write_time_fsp(fsp->base_fsp,
							  ft->mtime);
			} else {
				set_sticky_write_time_fsp(fsp, ft->mtime);
			}
		} else {
			set_sticky_write_time_path(conn, fname,
					    vfs_file_id_from_sbuf(conn, psbuf),
					    ft->mtime);
		}
	}

	DEBUG(10,("smb_set_file_time: setting utimes to modified values.\n"));

	if (fsp && fsp->base_fsp) {
		fname = fsp->base_fsp->fsp_name;
	}

	if(file_ntimes(conn, fname, ft)!=0) {
		return map_nt_error_from_unix(errno);
	}
	notify_fname(conn, NOTIFY_ACTION_MODIFIED, action, fname);

	return NT_STATUS_OK;
}

/****************************************************************************
 Deal with setting the dosmode from any of the setfilepathinfo functions.
****************************************************************************/

static NTSTATUS smb_set_file_dosmode(connection_struct *conn,
				files_struct *fsp,
				const char *fname,
				SMB_STRUCT_STAT *psbuf,
				uint32 dosmode)
{
	if (!VALID_STAT(*psbuf)) {
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	if (fsp) {
		if (fsp->base_fsp) {
			fname = fsp->base_fsp->fsp_name;
		} else {
			fname = fsp->fsp_name;
		}
	}
		
	if (dosmode) {
		if (S_ISDIR(psbuf->st_mode)) {
			dosmode |= aDIR;
		} else {
			dosmode &= ~aDIR;
		}
	}

	DEBUG(6,("smb_set_file_dosmode: dosmode: 0x%x\n", (unsigned int)dosmode));

	/* check the mode isn't different, before changing it */
	if ((dosmode != 0) && (dosmode != dos_mode(conn, fname, psbuf))) {

		DEBUG(10,("smb_set_file_dosmode: file %s : setting dos mode 0x%x\n",
					fname, (unsigned int)dosmode ));

		if(file_set_dosmode(conn, fname, dosmode, psbuf, NULL, false)) {
			DEBUG(2,("smb_set_file_dosmode: file_set_dosmode of %s failed (%s)\n",
						fname, strerror(errno)));
			return map_nt_error_from_unix(errno);
		}
	}
	return NT_STATUS_OK;
}

/****************************************************************************
 Deal with setting the size from any of the setfilepathinfo functions.
****************************************************************************/

static NTSTATUS smb_set_file_size(connection_struct *conn,
				  struct smb_request *req,
				files_struct *fsp,
				const char *fname,
				SMB_STRUCT_STAT *psbuf,
				SMB_OFF_T size)
{
	NTSTATUS status = NT_STATUS_OK;
	files_struct *new_fsp = NULL;

	if (!VALID_STAT(*psbuf)) {
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	DEBUG(6,("smb_set_file_size: size: %.0f ", (double)size));

	if (size == get_file_size_stat(psbuf)) {
		return NT_STATUS_OK;
	}

	DEBUG(10,("smb_set_file_size: file %s : setting new size to %.0f\n",
		fname, (double)size ));

	if (fsp && fsp->fh->fd != -1) {
		/* Handle based call. */
		if (vfs_set_filelen(fsp, size) == -1) {
			return map_nt_error_from_unix(errno);
		}
		trigger_write_time_update_immediate(fsp);
		return NT_STATUS_OK;
	}

        status = SMB_VFS_CREATE_FILE(
		conn,					/* conn */
		req,					/* req */
		0,					/* root_dir_fid */
		fname,					/* fname */
		0,					/* create_file_flags */
		FILE_WRITE_ATTRIBUTES,			/* access_mask */
		(FILE_SHARE_READ | FILE_SHARE_WRITE |	/* share_access */
		    FILE_SHARE_DELETE),
		FILE_OPEN,				/* create_disposition*/
		0,					/* create_options */
		FILE_ATTRIBUTE_NORMAL,			/* file_attributes */
		FORCE_OPLOCK_BREAK_TO_NONE,		/* oplock_request */
		0,					/* allocation_size */
		NULL,					/* sd */
		NULL,					/* ea_list */
		&new_fsp,				/* result */
		NULL,					/* pinfo */
		psbuf);					/* psbuf */

	if (!NT_STATUS_IS_OK(status)) {
		/* NB. We check for open_was_deferred in the caller. */
		return status;
	}

	if (vfs_set_filelen(new_fsp, size) == -1) {
		status = map_nt_error_from_unix(errno);
		close_file(req, new_fsp,NORMAL_CLOSE);
		return status;
	}

	trigger_write_time_update_immediate(new_fsp);
	close_file(req, new_fsp,NORMAL_CLOSE);
	return NT_STATUS_OK;
}

/****************************************************************************
 Deal with SMB_INFO_SET_EA.
****************************************************************************/

static NTSTATUS smb_info_set_ea(connection_struct *conn,
				const char *pdata,
				int total_data,
				files_struct *fsp,
				const char *fname)
{
	struct ea_list *ea_list = NULL;
	TALLOC_CTX *ctx = NULL;
	NTSTATUS status = NT_STATUS_OK;

	if (total_data < 10) {

		/* OS/2 workplace shell seems to send SET_EA requests of "null"
		   length. They seem to have no effect. Bug #3212. JRA */

		if ((total_data == 4) && (IVAL(pdata,0) == 4)) {
			/* We're done. We only get EA info in this call. */
			return NT_STATUS_OK;
		}

		return NT_STATUS_INVALID_PARAMETER;
	}

	if (IVAL(pdata,0) > total_data) {
		DEBUG(10,("smb_info_set_ea: bad total data size (%u) > %u\n",
			IVAL(pdata,0), (unsigned int)total_data));
		return NT_STATUS_INVALID_PARAMETER;
	}

	ctx = talloc_tos();
	ea_list = read_ea_list(ctx, pdata + 4, total_data - 4);
	if (!ea_list) {
		return NT_STATUS_INVALID_PARAMETER;
	}
	status = set_ea(conn, fsp, fname, ea_list);

	return status;
}

/****************************************************************************
 Deal with SMB_SET_FILE_DISPOSITION_INFO.
****************************************************************************/

static NTSTATUS smb_set_file_disposition_info(connection_struct *conn,
				const char *pdata,
				int total_data,
				files_struct *fsp,
				const char *fname,
				SMB_STRUCT_STAT *psbuf)
{
	NTSTATUS status = NT_STATUS_OK;
	bool delete_on_close;
	uint32 dosmode = 0;

	if (total_data < 1) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (fsp == NULL) {
		return NT_STATUS_INVALID_HANDLE;
	}

	delete_on_close = (CVAL(pdata,0) ? True : False);
	dosmode = dos_mode(conn, fname, psbuf);

	DEBUG(10,("smb_set_file_disposition_info: file %s, dosmode = %u, "
		"delete_on_close = %u\n",
		fsp->fsp_name,
		(unsigned int)dosmode,
		(unsigned int)delete_on_close ));

	status = can_set_delete_on_close(fsp, delete_on_close, dosmode);
 
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/* The set is across all open files on this dev/inode pair. */
	if (!set_delete_on_close(fsp, delete_on_close,
				 &conn->server_info->utok)) {
		return NT_STATUS_ACCESS_DENIED;
	}
	return NT_STATUS_OK;
}

/****************************************************************************
 Deal with SMB_FILE_POSITION_INFORMATION.
****************************************************************************/

static NTSTATUS smb_file_position_information(connection_struct *conn,
				const char *pdata,
				int total_data,
				files_struct *fsp)
{
	uint64_t position_information;

	if (total_data < 8) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (fsp == NULL) {
		/* Ignore on pathname based set. */
		return NT_STATUS_OK;
	}

	position_information = (uint64_t)IVAL(pdata,0);
#ifdef LARGE_SMB_OFF_T
	position_information |= (((uint64_t)IVAL(pdata,4)) << 32);
#else /* LARGE_SMB_OFF_T */
	if (IVAL(pdata,4) != 0) {
		/* more than 32 bits? */
		return NT_STATUS_INVALID_PARAMETER;
	}
#endif /* LARGE_SMB_OFF_T */

	DEBUG(10,("smb_file_position_information: Set file position information for file %s to %.0f\n",
		fsp->fsp_name, (double)position_information ));
	fsp->fh->position_information = position_information;
	return NT_STATUS_OK;
}

/****************************************************************************
 Deal with SMB_FILE_MODE_INFORMATION.
****************************************************************************/

static NTSTATUS smb_file_mode_information(connection_struct *conn,
				const char *pdata,
				int total_data)
{
	uint32 mode;

	if (total_data < 4) {
		return NT_STATUS_INVALID_PARAMETER;
	}
	mode = IVAL(pdata,0);
	if (mode != 0 && mode != 2 && mode != 4 && mode != 6) {
		return NT_STATUS_INVALID_PARAMETER;
	}
	return NT_STATUS_OK;
}

/****************************************************************************
 Deal with SMB_SET_FILE_UNIX_LINK (create a UNIX symlink).
****************************************************************************/

static NTSTATUS smb_set_file_unix_link(connection_struct *conn,
				       struct smb_request *req,
				       const char *pdata,
				       int total_data,
				       const char *fname)
{
	char *link_target = NULL;
	const char *newname = fname;
	NTSTATUS status = NT_STATUS_OK;
	TALLOC_CTX *ctx = talloc_tos();

	/* Set a symbolic link. */
	/* Don't allow this if follow links is false. */

	if (total_data == 0) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (!lp_symlinks(SNUM(conn))) {
		return NT_STATUS_ACCESS_DENIED;
	}

	srvstr_pull_talloc(ctx, pdata, req->flags2, &link_target, pdata,
		    total_data, STR_TERMINATE);

	if (!link_target) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	/* !widelinks forces the target path to be within the share. */
	/* This means we can interpret the target as a pathname. */
	if (!lp_widelinks(SNUM(conn))) {
		char *rel_name = NULL;
		char *last_dirp = NULL;

		if (*link_target == '/') {
			/* No absolute paths allowed. */
			return NT_STATUS_ACCESS_DENIED;
		}
		rel_name = talloc_strdup(ctx,newname);
		if (!rel_name) {
			return NT_STATUS_NO_MEMORY;
		}
		last_dirp = strrchr_m(rel_name, '/');
		if (last_dirp) {
			last_dirp[1] = '\0';
		} else {
			rel_name = talloc_strdup(ctx,"./");
			if (!rel_name) {
				return NT_STATUS_NO_MEMORY;
			}
		}
		rel_name = talloc_asprintf_append(rel_name,
				"%s",
				link_target);
		if (!rel_name) {
			return NT_STATUS_NO_MEMORY;
		}

		status = check_name(conn, rel_name);
		if (!NT_STATUS_IS_OK(status)) {
			return status;
		}
	}

	DEBUG(10,("smb_set_file_unix_link: SMB_SET_FILE_UNIX_LINK doing symlink %s -> %s\n",
			newname, link_target ));

	if (SMB_VFS_SYMLINK(conn,link_target,newname) != 0) {
		return map_nt_error_from_unix(errno);
	}

	return NT_STATUS_OK;
}

/****************************************************************************
 Deal with SMB_SET_FILE_UNIX_HLINK (create a UNIX hard link).
****************************************************************************/

static NTSTATUS smb_set_file_unix_hlink(connection_struct *conn,
					struct smb_request *req,
					const char *pdata, int total_data,
					const char *fname)
{
	char *oldname = NULL;
	TALLOC_CTX *ctx = talloc_tos();
	NTSTATUS status = NT_STATUS_OK;

	/* Set a hard link. */
	if (total_data == 0) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	srvstr_get_path(ctx, pdata, req->flags2, &oldname, pdata,
			total_data, STR_TERMINATE, &status);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	status = resolve_dfspath(ctx, conn,
				req->flags2 & FLAGS2_DFS_PATHNAMES,
				oldname,
				&oldname);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	DEBUG(10,("smb_set_file_unix_hlink: SMB_SET_FILE_UNIX_LINK doing hard link %s -> %s\n",
		fname, oldname));

	return hardlink_internals(ctx, conn, oldname, fname);
}

/****************************************************************************
 Deal with SMB_FILE_RENAME_INFORMATION.
****************************************************************************/

static NTSTATUS smb_file_rename_information(connection_struct *conn,
					    struct smb_request *req,
					    const char *pdata,
					    int total_data,
					    files_struct *fsp,
					    const char *fname)
{
	bool overwrite;
	uint32 root_fid;
	uint32 len;
	char *newname = NULL;
	char *base_name = NULL;
	bool dest_has_wcard = False;
	SMB_STRUCT_STAT sbuf;
	char *newname_last_component = NULL;
	NTSTATUS status = NT_STATUS_OK;
	char *p;
	TALLOC_CTX *ctx = talloc_tos();

	if (total_data < 13) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	ZERO_STRUCT(sbuf);

	overwrite = (CVAL(pdata,0) ? True : False);
	root_fid = IVAL(pdata,4);
	len = IVAL(pdata,8);

	if (len > (total_data - 12) || (len == 0) || (root_fid != 0)) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	srvstr_get_path_wcard(ctx, pdata, req->flags2, &newname, &pdata[12],
			      len, 0, &status,
			      &dest_has_wcard);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	DEBUG(10,("smb_file_rename_information: got name |%s|\n",
				newname));

	status = resolve_dfspath_wcard(ctx, conn,
				       req->flags2 & FLAGS2_DFS_PATHNAMES,
				       newname,
				       &newname,
				       &dest_has_wcard);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/* Check the new name has no '/' characters. */
	if (strchr_m(newname, '/')) {
		return NT_STATUS_NOT_SUPPORTED;
	}

	if (fsp && fsp->base_fsp) {
		/* newname must be a stream name. */
		if (newname[0] != ':') {
			return NT_STATUS_NOT_SUPPORTED;
		}
		base_name = talloc_asprintf(ctx, "%s%s",
					   fsp->base_fsp->fsp_name,
					   newname);
		if (!base_name) {
			return NT_STATUS_NO_MEMORY;
		}
	} else {
		/* newname must *not* be a stream name. */
		if (is_ntfs_stream_name(newname)) {
			return NT_STATUS_NOT_SUPPORTED;
		}

		/* Create the base directory. */
		base_name = talloc_strdup(ctx, fname);
		if (!base_name) {
			return NT_STATUS_NO_MEMORY;
		}
		p = strrchr_m(base_name, '/');
		if (p) {
			p[1] = '\0';
		} else {
			base_name = talloc_strdup(ctx, "./");
			if (!base_name) {
				return NT_STATUS_NO_MEMORY;
			}
		}
		/* Append the new name. */
		base_name = talloc_asprintf_append(base_name,
				"%s",
				newname);
		if (!base_name) {
			return NT_STATUS_NO_MEMORY;
		}

		status = unix_convert(ctx, conn, newname, False,
				&newname,
				&newname_last_component,
				&sbuf);

		/* If an error we expect this to be
		 * NT_STATUS_OBJECT_PATH_NOT_FOUND */

		if (!NT_STATUS_IS_OK(status)
		    && !NT_STATUS_EQUAL(NT_STATUS_OBJECT_PATH_NOT_FOUND,
					status)) {
			return status;
		}
	}

	if (fsp) {
		DEBUG(10,("smb_file_rename_information: SMB_FILE_RENAME_INFORMATION (fnum %d) %s -> %s\n",
			fsp->fnum, fsp->fsp_name, base_name ));
		status = rename_internals_fsp(conn, fsp, base_name,
					      newname_last_component, 0,
					      overwrite);
	} else {
		DEBUG(10,("smb_file_rename_information: SMB_FILE_RENAME_INFORMATION %s -> %s\n",
			fname, base_name ));
		status = rename_internals(ctx, conn, req, fname, base_name, 0,
					overwrite, False, dest_has_wcard,
					FILE_WRITE_ATTRIBUTES);
	}

	return status;
}

/****************************************************************************
 Deal with SMB_SET_POSIX_ACL.
****************************************************************************/

#if defined(HAVE_POSIX_ACLS)
static NTSTATUS smb_set_posix_acl(connection_struct *conn,
				const char *pdata,
				int total_data,
				files_struct *fsp,
				const char *fname,
				SMB_STRUCT_STAT *psbuf)
{
	uint16 posix_acl_version;
	uint16 num_file_acls;
	uint16 num_def_acls;
	bool valid_file_acls = True;
	bool valid_def_acls = True;

	if (total_data < SMB_POSIX_ACL_HEADER_SIZE) {
		return NT_STATUS_INVALID_PARAMETER;
	}
	posix_acl_version = SVAL(pdata,0);
	num_file_acls = SVAL(pdata,2);
	num_def_acls = SVAL(pdata,4);

	if (num_file_acls == SMB_POSIX_IGNORE_ACE_ENTRIES) {
		valid_file_acls = False;
		num_file_acls = 0;
	}

	if (num_def_acls == SMB_POSIX_IGNORE_ACE_ENTRIES) {
		valid_def_acls = False;
		num_def_acls = 0;
	}

	if (posix_acl_version != SMB_POSIX_ACL_VERSION) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (total_data < SMB_POSIX_ACL_HEADER_SIZE +
			(num_file_acls+num_def_acls)*SMB_POSIX_ACL_ENTRY_SIZE) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	DEBUG(10,("smb_set_posix_acl: file %s num_file_acls = %u, num_def_acls = %u\n",
		fname ? fname : fsp->fsp_name,
		(unsigned int)num_file_acls,
		(unsigned int)num_def_acls));

	if (valid_file_acls && !set_unix_posix_acl(conn, fsp, fname, num_file_acls,
			pdata + SMB_POSIX_ACL_HEADER_SIZE)) {
		return map_nt_error_from_unix(errno);
	}

	if (valid_def_acls && !set_unix_posix_default_acl(conn, fname, psbuf, num_def_acls,
			pdata + SMB_POSIX_ACL_HEADER_SIZE +
			(num_file_acls*SMB_POSIX_ACL_ENTRY_SIZE))) {
		return map_nt_error_from_unix(errno);
	}
	return NT_STATUS_OK;
}
#endif

/****************************************************************************
 Deal with SMB_SET_POSIX_LOCK.
****************************************************************************/

static NTSTATUS smb_set_posix_lock(connection_struct *conn,
				struct smb_request *req,
				const char *pdata,
				int total_data,
				files_struct *fsp)
{
	uint64_t count;
	uint64_t offset;
	uint32 lock_pid;
	bool blocking_lock = False;
	enum brl_type lock_type;

	NTSTATUS status = NT_STATUS_OK;

	if (fsp == NULL || fsp->fh->fd == -1) {
		return NT_STATUS_INVALID_HANDLE;
	}

	if (total_data != POSIX_LOCK_DATA_SIZE) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	switch (SVAL(pdata, POSIX_LOCK_TYPE_OFFSET)) {
		case POSIX_LOCK_TYPE_READ:
			lock_type = READ_LOCK;
			break;
		case POSIX_LOCK_TYPE_WRITE:
			/* Return the right POSIX-mappable error code for files opened read-only. */
			if (!fsp->can_write) {
				return NT_STATUS_INVALID_HANDLE;
			}
			lock_type = WRITE_LOCK;
			break;
		case POSIX_LOCK_TYPE_UNLOCK:
			lock_type = UNLOCK_LOCK;
			break;
		default:
			return NT_STATUS_INVALID_PARAMETER;
	}

	if (SVAL(pdata,POSIX_LOCK_FLAGS_OFFSET) == POSIX_LOCK_FLAG_NOWAIT) {
		blocking_lock = False;
	} else if (SVAL(pdata,POSIX_LOCK_FLAGS_OFFSET) == POSIX_LOCK_FLAG_WAIT) {
		blocking_lock = True;
	} else {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (!lp_blocking_locks(SNUM(conn))) { 
		blocking_lock = False;
	}

	lock_pid = IVAL(pdata, POSIX_LOCK_PID_OFFSET);
#if defined(HAVE_LONGLONG)
	offset = (((uint64_t) IVAL(pdata,(POSIX_LOCK_START_OFFSET+4))) << 32) |
			((uint64_t) IVAL(pdata,POSIX_LOCK_START_OFFSET));
	count = (((uint64_t) IVAL(pdata,(POSIX_LOCK_LEN_OFFSET+4))) << 32) |
			((uint64_t) IVAL(pdata,POSIX_LOCK_LEN_OFFSET));
#else /* HAVE_LONGLONG */
	offset = (uint64_t)IVAL(pdata,POSIX_LOCK_START_OFFSET);
	count = (uint64_t)IVAL(pdata,POSIX_LOCK_LEN_OFFSET);
#endif /* HAVE_LONGLONG */

	DEBUG(10,("smb_set_posix_lock: file %s, lock_type = %u,"
			"lock_pid = %u, count = %.0f, offset = %.0f\n",
		fsp->fsp_name,
		(unsigned int)lock_type,
		(unsigned int)lock_pid,
		(double)count,
		(double)offset ));

	if (lock_type == UNLOCK_LOCK) {
		status = do_unlock(smbd_messaging_context(),
				fsp,
				lock_pid,
				count,
				offset,
				POSIX_LOCK);
	} else {
		uint32 block_smbpid;

		struct byte_range_lock *br_lck = do_lock(smbd_messaging_context(),
							fsp,
							lock_pid,
							count,
							offset,
							lock_type,
							POSIX_LOCK,
							blocking_lock,
							&status,
							&block_smbpid,
							NULL);

		if (br_lck && blocking_lock && ERROR_WAS_LOCK_DENIED(status)) {
			/*
			 * A blocking lock was requested. Package up
			 * this smb into a queued request and push it
			 * onto the blocking lock queue.
			 */
			if(push_blocking_lock_request(br_lck,
						req,
						fsp,
						-1, /* infinite timeout. */
						0,
						lock_pid,
						lock_type,
						POSIX_LOCK,
						offset,
						count,
						block_smbpid)) {
				TALLOC_FREE(br_lck);
				return status;
			}
		}
		TALLOC_FREE(br_lck);
	}

	return status;
}

/****************************************************************************
 Deal with SMB_INFO_STANDARD.
****************************************************************************/

static NTSTATUS smb_set_info_standard(connection_struct *conn,
					const char *pdata,
					int total_data,
					files_struct *fsp,
					const char *fname,
					const SMB_STRUCT_STAT *psbuf)
{
	struct smb_file_time ft;
	ZERO_STRUCT(ft);

	if (total_data < 12) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	/* create time */
	ft.create_time = convert_time_t_to_timespec(srv_make_unix_date2(pdata));

	/* access time */
	ft.atime = convert_time_t_to_timespec(srv_make_unix_date2(pdata+4));

	/* write time */
	ft.mtime = convert_time_t_to_timespec(srv_make_unix_date2(pdata+8));

	DEBUG(10,("smb_set_info_standard: file %s\n",
		fname ? fname : fsp->fsp_name ));

	return smb_set_file_time(conn,
				fsp,
				fname,
				psbuf,
				&ft,
				true);
}

/****************************************************************************
 Deal with SMB_SET_FILE_BASIC_INFO.
****************************************************************************/

static NTSTATUS smb_set_file_basic_info(connection_struct *conn,
					const char *pdata,
					int total_data,
					files_struct *fsp,
					const char *fname,
					SMB_STRUCT_STAT *psbuf)
{
	/* Patch to do this correctly from Paul Eggert <eggert@twinsun.com>. */
	struct timespec write_time;
	struct timespec changed_time;
	struct smb_file_time ft;
	uint32 dosmode = 0;
	NTSTATUS status = NT_STATUS_OK;
	bool setting_write_time = true;

	ZERO_STRUCT(ft);

	if (total_data < 36) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	/* Set the attributes */
	dosmode = IVAL(pdata,32);
	status = smb_set_file_dosmode(conn, fsp, fname, psbuf, dosmode);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/* access time */
	ft.atime = interpret_long_date(pdata+8);

	write_time = interpret_long_date(pdata+16);
	changed_time = interpret_long_date(pdata+24);

	/* mtime */
	ft.mtime = timespec_min(&write_time, &changed_time);

	/* create time */
	ft.create_time = interpret_long_date(pdata);

	if ((timespec_compare(&write_time, &ft.mtime) == 1) &&
	    !null_timespec(write_time)) {
		ft.mtime = write_time;
	}

	/* Prefer a defined time to an undefined one. */
	if (null_timespec(ft.mtime)) {
		if (null_timespec(write_time)) {
			ft.mtime = changed_time;
			setting_write_time = false;
		} else {
			ft.mtime = write_time;
		}
	}

	DEBUG(10,("smb_set_file_basic_info: file %s\n",
		fname ? fname : fsp->fsp_name ));

	return smb_set_file_time(conn,
				fsp,
				fname,
				psbuf,
				&ft,
				setting_write_time);
}

/****************************************************************************
 Deal with SMB_SET_FILE_ALLOCATION_INFO.
****************************************************************************/

static NTSTATUS smb_set_file_allocation_info(connection_struct *conn,
					     struct smb_request *req,
					const char *pdata,
					int total_data,
					files_struct *fsp,
					const char *fname,
					SMB_STRUCT_STAT *psbuf)
{
	uint64_t allocation_size = 0;
	NTSTATUS status = NT_STATUS_OK;
	files_struct *new_fsp = NULL;

	if (!VALID_STAT(*psbuf)) {
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	if (total_data < 8) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	allocation_size = (uint64_t)IVAL(pdata,0);
#ifdef LARGE_SMB_OFF_T
	allocation_size |= (((uint64_t)IVAL(pdata,4)) << 32);
#else /* LARGE_SMB_OFF_T */
	if (IVAL(pdata,4) != 0) {
		/* more than 32 bits? */
		return NT_STATUS_INVALID_PARAMETER;
	}
#endif /* LARGE_SMB_OFF_T */

	DEBUG(10,("smb_set_file_allocation_info: Set file allocation info for file %s to %.0f\n",
			fname, (double)allocation_size ));

	if (allocation_size) {
		allocation_size = smb_roundup(conn, allocation_size);
	}

	DEBUG(10,("smb_set_file_allocation_info: file %s : setting new allocation size to %.0f\n",
			fname, (double)allocation_size ));

	if (fsp && fsp->fh->fd != -1) {
		/* Open file handle. */
		/* Only change if needed. */
		if (allocation_size != get_file_size_stat(psbuf)) {
			if (vfs_allocate_file_space(fsp, allocation_size) == -1) {
				return map_nt_error_from_unix(errno);
			}
		}
		/* But always update the time. */
		/*
		 * This is equivalent to a write. Ensure it's seen immediately
		 * if there are no pending writes.
		 */
		trigger_write_time_update_immediate(fsp);
		return NT_STATUS_OK;
	}

	/* Pathname or stat or directory file. */

	status = SMB_VFS_CREATE_FILE(
		conn,					/* conn */
		req,					/* req */
		0,					/* root_dir_fid */
		fname,					/* fname */
		0,					/* create_file_flags */
		FILE_WRITE_DATA,			/* access_mask */
		(FILE_SHARE_READ | FILE_SHARE_WRITE |	/* share_access */
		    FILE_SHARE_DELETE),
		FILE_OPEN,				/* create_disposition*/
		0,					/* create_options */
		FILE_ATTRIBUTE_NORMAL,			/* file_attributes */
		FORCE_OPLOCK_BREAK_TO_NONE,		/* oplock_request */
		0,					/* allocation_size */
		NULL,					/* sd */
		NULL,					/* ea_list */
		&new_fsp,				/* result */
		NULL,					/* pinfo */
		psbuf);					/* psbuf */

	if (!NT_STATUS_IS_OK(status)) {
		/* NB. We check for open_was_deferred in the caller. */
		return status;
	}

	/* Only change if needed. */
	if (allocation_size != get_file_size_stat(psbuf)) {
		if (vfs_allocate_file_space(new_fsp, allocation_size) == -1) {
			status = map_nt_error_from_unix(errno);
			close_file(req, new_fsp, NORMAL_CLOSE);
			return status;
		}
	}

	/* Changing the allocation size should set the last mod time. */
	/*
	 * This is equivalent to a write. Ensure it's seen immediately
	 * if there are no pending writes.
	 */
	trigger_write_time_update_immediate(new_fsp);

	close_file(req, new_fsp, NORMAL_CLOSE);
	return NT_STATUS_OK;
}

/****************************************************************************
 Deal with SMB_SET_FILE_END_OF_FILE_INFO.
****************************************************************************/

static NTSTATUS smb_set_file_end_of_file_info(connection_struct *conn,
					      struct smb_request *req,
					const char *pdata,
					int total_data,
					files_struct *fsp,
					const char *fname,
					SMB_STRUCT_STAT *psbuf)
{
	SMB_OFF_T size;

	if (total_data < 8) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	size = IVAL(pdata,0);
#ifdef LARGE_SMB_OFF_T
	size |= (((SMB_OFF_T)IVAL(pdata,4)) << 32);
#else /* LARGE_SMB_OFF_T */
	if (IVAL(pdata,4) != 0)	{
		/* more than 32 bits? */
		return NT_STATUS_INVALID_PARAMETER;
	}
#endif /* LARGE_SMB_OFF_T */
	DEBUG(10,("smb_set_file_end_of_file_info: Set end of file info for "
		"file %s to %.0f\n", fname, (double)size ));

	return smb_set_file_size(conn, req,
				fsp,
				fname,
				psbuf,
				size);
}

/****************************************************************************
 Allow a UNIX info mknod.
****************************************************************************/

static NTSTATUS smb_unix_mknod(connection_struct *conn,
					const char *pdata,
					int total_data,
					const char *fname,
					SMB_STRUCT_STAT *psbuf)
{
	uint32 file_type = IVAL(pdata,56);
#if defined(HAVE_MAKEDEV)
	uint32 dev_major = IVAL(pdata,60);
	uint32 dev_minor = IVAL(pdata,68);
#endif
	SMB_DEV_T dev = (SMB_DEV_T)0;
	uint32 raw_unixmode = IVAL(pdata,84);
	NTSTATUS status;
	mode_t unixmode;

	if (total_data < 100) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	status = unix_perms_from_wire(conn, psbuf, raw_unixmode, PERM_NEW_FILE, &unixmode);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

#if defined(HAVE_MAKEDEV)
	dev = makedev(dev_major, dev_minor);
#endif

	switch (file_type) {
#if defined(S_IFIFO)
		case UNIX_TYPE_FIFO:
			unixmode |= S_IFIFO;
			break;
#endif
#if defined(S_IFSOCK)
		case UNIX_TYPE_SOCKET:
			unixmode |= S_IFSOCK;
			break;
#endif
#if defined(S_IFCHR)
		case UNIX_TYPE_CHARDEV:
			unixmode |= S_IFCHR;
			break;
#endif
#if defined(S_IFBLK)
		case UNIX_TYPE_BLKDEV:
			unixmode |= S_IFBLK;
			break;
#endif
		default:
			return NT_STATUS_INVALID_PARAMETER;
	}

	DEBUG(10,("smb_unix_mknod: SMB_SET_FILE_UNIX_BASIC doing mknod dev %.0f mode \
0%o for file %s\n", (double)dev, (unsigned int)unixmode, fname ));

	/* Ok - do the mknod. */
	if (SMB_VFS_MKNOD(conn, fname, unixmode, dev) != 0) {
		return map_nt_error_from_unix(errno);
	}

	/* If any of the other "set" calls fail we
 	 * don't want to end up with a half-constructed mknod.
 	 */

	if (lp_inherit_perms(SNUM(conn))) {
		char *parent;
		if (!parent_dirname(talloc_tos(), fname, &parent, NULL)) {
			return NT_STATUS_NO_MEMORY;
		}
		inherit_access_posix_acl(conn, parent, fname, unixmode);
		TALLOC_FREE(parent);
	}

	if (SMB_VFS_STAT(conn, fname, psbuf) != 0) {
		status = map_nt_error_from_unix(errno);
		SMB_VFS_UNLINK(conn,fname);
		return status;
	}
	return NT_STATUS_OK;
}

/****************************************************************************
 Deal with SMB_SET_FILE_UNIX_BASIC.
****************************************************************************/

static NTSTATUS smb_set_file_unix_basic(connection_struct *conn,
					struct smb_request *req,
					const char *pdata,
					int total_data,
					files_struct *fsp,
					const char *fname,
					SMB_STRUCT_STAT *psbuf)
{
	struct smb_file_time ft;
	uint32 raw_unixmode;
	mode_t unixmode;
	SMB_OFF_T size = 0;
	uid_t set_owner = (uid_t)SMB_UID_NO_CHANGE;
	gid_t set_grp = (uid_t)SMB_GID_NO_CHANGE;
	NTSTATUS status = NT_STATUS_OK;
	bool delete_on_fail = False;
	enum perm_type ptype;
	files_struct *all_fsps = NULL;
	bool modify_mtime = true;
	struct file_id id;

	ZERO_STRUCT(ft);

	if (total_data < 100) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if(IVAL(pdata, 0) != SMB_SIZE_NO_CHANGE_LO &&
	   IVAL(pdata, 4) != SMB_SIZE_NO_CHANGE_HI) {
		size=IVAL(pdata,0); /* first 8 Bytes are size */
#ifdef LARGE_SMB_OFF_T
		size |= (((SMB_OFF_T)IVAL(pdata,4)) << 32);
#else /* LARGE_SMB_OFF_T */
		if (IVAL(pdata,4) != 0)	{
			/* more than 32 bits? */
			return NT_STATUS_INVALID_PARAMETER;
		}
#endif /* LARGE_SMB_OFF_T */
	}

	ft.atime = interpret_long_date(pdata+24); /* access_time */
	ft.mtime = interpret_long_date(pdata+32); /* modification_time */
	set_owner = (uid_t)IVAL(pdata,40);
	set_grp = (gid_t)IVAL(pdata,48);
	raw_unixmode = IVAL(pdata,84);

	if (VALID_STAT(*psbuf)) {
		if (S_ISDIR(psbuf->st_mode)) {
			ptype = PERM_EXISTING_DIR;
		} else {
			ptype = PERM_EXISTING_FILE;
		}
	} else {
		ptype = PERM_NEW_FILE;
	}

	status = unix_perms_from_wire(conn, psbuf, raw_unixmode, ptype, &unixmode);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	DEBUG(10,("smb_set_file_unix_basic: SMB_SET_FILE_UNIX_BASIC: name = %s \
size = %.0f, uid = %u, gid = %u, raw perms = 0%o\n",
		fname, (double)size, (unsigned int)set_owner, (unsigned int)set_grp, (int)raw_unixmode));

	if (!VALID_STAT(*psbuf)) {
		/*
		 * The only valid use of this is to create character and block
		 * devices, and named pipes. This is deprecated (IMHO) and 
		 * a new info level should be used for mknod. JRA.
		 */

		status = smb_unix_mknod(conn,
					pdata,
					total_data,
					fname,
					psbuf);
		if (!NT_STATUS_IS_OK(status)) {
			return status;
		}

		/* Ensure we don't try and change anything else. */
		raw_unixmode = SMB_MODE_NO_CHANGE;
		size = get_file_size_stat(psbuf);
		ft.atime = get_atimespec(psbuf);
		ft.mtime = get_mtimespec(psbuf);
		/* 
		 * We continue here as we might want to change the 
		 * owner uid/gid.
		 */
		delete_on_fail = True;
	}

#if 1
	/* Horrible backwards compatibility hack as an old server bug
	 * allowed a CIFS client bug to remain unnoticed :-(. JRA.
	 * */

	if (!size) {
		size = get_file_size_stat(psbuf);
	}
#endif

	/*
	 * Deal with the UNIX specific mode set.
	 */

	if (raw_unixmode != SMB_MODE_NO_CHANGE) {
		DEBUG(10,("smb_set_file_unix_basic: SMB_SET_FILE_UNIX_BASIC setting mode 0%o for file %s\n",
			(unsigned int)unixmode, fname ));
		if (SMB_VFS_CHMOD(conn, fname, unixmode) != 0) {
			return map_nt_error_from_unix(errno);
		}
	}

	/*
	 * Deal with the UNIX specific uid set.
	 */

	if ((set_owner != (uid_t)SMB_UID_NO_CHANGE) && (psbuf->st_uid != set_owner)) {
		int ret;

		DEBUG(10,("smb_set_file_unix_basic: SMB_SET_FILE_UNIX_BASIC changing owner %u for path %s\n",
			(unsigned int)set_owner, fname ));

		if (S_ISLNK(psbuf->st_mode)) {
			ret = SMB_VFS_LCHOWN(conn, fname, set_owner, (gid_t)-1);
		} else {
			ret = SMB_VFS_CHOWN(conn, fname, set_owner, (gid_t)-1);
		}

		if (ret != 0) {
			status = map_nt_error_from_unix(errno);
			if (delete_on_fail) {
				SMB_VFS_UNLINK(conn,fname);
			}
			return status;
		}
	}

	/*
	 * Deal with the UNIX specific gid set.
	 */

	if ((set_grp != (uid_t)SMB_GID_NO_CHANGE) && (psbuf->st_gid != set_grp)) {
		DEBUG(10,("smb_set_file_unix_basic: SMB_SET_FILE_UNIX_BASIC changing group %u for file %s\n",
			(unsigned int)set_owner, fname ));
		if (SMB_VFS_CHOWN(conn, fname, (uid_t)-1, set_grp) != 0) {
			status = map_nt_error_from_unix(errno);
			if (delete_on_fail) {
				SMB_VFS_UNLINK(conn,fname);
			}
			return status;
		}
	}

	/* Deal with any size changes. */

	status = smb_set_file_size(conn, req,
				fsp,
				fname,
				psbuf,
				size);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/* Deal with any time changes. */
	id = vfs_file_id_from_sbuf(conn, psbuf);
	for(all_fsps = file_find_di_first(id); all_fsps;
			all_fsps = file_find_di_next(all_fsps)) {
		/*
		 * We're setting the time explicitly for UNIX.
		 * Cancel any pending changes over all handles.
		 */
		all_fsps->update_write_time_on_close = false;
		TALLOC_FREE(all_fsps->update_write_time_event);
	}

	/*
	 * Override the "setting_write_time"
	 * parameter here as it almost does what
	 * we need. Just remember if we modified
	 * mtime and send the notify ourselves.
	 */
	if (null_timespec(ft.mtime)) {
		modify_mtime = false;
	}

	status = smb_set_file_time(conn,
				fsp,
				fname,
				psbuf,
				&ft,
				false);
	if (modify_mtime) {
		notify_fname(conn, NOTIFY_ACTION_MODIFIED,
			FILE_NOTIFY_CHANGE_LAST_WRITE, fname);
	}
	return status;
}

/****************************************************************************
 Deal with SMB_SET_FILE_UNIX_INFO2.
****************************************************************************/

static NTSTATUS smb_set_file_unix_info2(connection_struct *conn,
					struct smb_request *req,
					const char *pdata,
					int total_data,
					files_struct *fsp,
					const char *fname,
					SMB_STRUCT_STAT *psbuf)
{
	NTSTATUS status;
	uint32 smb_fflags;
	uint32 smb_fmask;

	if (total_data < 116) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	/* Start by setting all the fields that are common between UNIX_BASIC
	 * and UNIX_INFO2.
	 */
	status = smb_set_file_unix_basic(conn, req, pdata, total_data,
				fsp, fname, psbuf);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	smb_fflags = IVAL(pdata, 108);
	smb_fmask = IVAL(pdata, 112);

	/* NB: We should only attempt to alter the file flags if the client
	 * sends a non-zero mask.
	 */
	if (smb_fmask != 0) {
		int stat_fflags = 0;

		if (!map_info2_flags_to_sbuf(psbuf, smb_fflags, smb_fmask,
			    &stat_fflags)) {
			/* Client asked to alter a flag we don't understand. */
			return NT_STATUS_INVALID_PARAMETER;
		}

		if (fsp && fsp->fh->fd != -1) {
			/* XXX: we should be  using SMB_VFS_FCHFLAGS here. */
			return NT_STATUS_NOT_SUPPORTED;
		} else {
			if (SMB_VFS_CHFLAGS(conn, fname, stat_fflags) != 0) {
				return map_nt_error_from_unix(errno);
			}
		}
	}

	/* XXX: need to add support for changing the create_time here. You
	 * can do this for paths on Darwin with setattrlist(2). The right way
	 * to hook this up is probably by extending the VFS utimes interface.
	 */

	return NT_STATUS_OK;
}

/****************************************************************************
 Create a directory with POSIX semantics.
****************************************************************************/

static NTSTATUS smb_posix_mkdir(connection_struct *conn,
				struct smb_request *req,
				char **ppdata,
				int total_data,
				const char *fname,
				SMB_STRUCT_STAT *psbuf,
				int *pdata_return_size)
{
	NTSTATUS status = NT_STATUS_OK;
	uint32 raw_unixmode = 0;
	uint32 mod_unixmode = 0;
	mode_t unixmode = (mode_t)0;
	files_struct *fsp = NULL;
	uint16 info_level_return = 0;
	int info;
	char *pdata = *ppdata;

	if (total_data < 18) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	raw_unixmode = IVAL(pdata,8);
	/* Next 4 bytes are not yet defined. */

	status = unix_perms_from_wire(conn, psbuf, raw_unixmode, PERM_NEW_DIR, &unixmode);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	mod_unixmode = (uint32)unixmode | FILE_FLAG_POSIX_SEMANTICS;

	DEBUG(10,("smb_posix_mkdir: file %s, mode 0%o\n",
		fname, (unsigned int)unixmode ));

	status = SMB_VFS_CREATE_FILE(
		conn,					/* conn */
		req,					/* req */
		0,					/* root_dir_fid */
		fname,					/* fname */
		0,					/* create_file_flags */
		FILE_READ_ATTRIBUTES,			/* access_mask */
		FILE_SHARE_NONE,			/* share_access */
		FILE_CREATE,				/* create_disposition*/
		FILE_DIRECTORY_FILE,			/* create_options */
		mod_unixmode,				/* file_attributes */
		0,					/* oplock_request */
		0,					/* allocation_size */
		NULL,					/* sd */
		NULL,					/* ea_list */
		&fsp,					/* result */
		&info,					/* pinfo */
		psbuf);					/* psbuf */

        if (NT_STATUS_IS_OK(status)) {
                close_file(req, fsp, NORMAL_CLOSE);
        }

	info_level_return = SVAL(pdata,16);
 
	if (info_level_return == SMB_QUERY_FILE_UNIX_BASIC) {
		*pdata_return_size = 12 + SMB_FILE_UNIX_BASIC_SIZE;
	} else if (info_level_return ==  SMB_QUERY_FILE_UNIX_INFO2) {
		*pdata_return_size = 12 + SMB_FILE_UNIX_INFO2_SIZE;
	} else {
		*pdata_return_size = 12;
	}

	/* Realloc the data size */
	*ppdata = (char *)SMB_REALLOC(*ppdata,*pdata_return_size);
	if (*ppdata == NULL) {
		*pdata_return_size = 0;
		return NT_STATUS_NO_MEMORY;
	}
	pdata = *ppdata;

	SSVAL(pdata,0,NO_OPLOCK_RETURN);
	SSVAL(pdata,2,0); /* No fnum. */
	SIVAL(pdata,4,info); /* Was directory created. */

	switch (info_level_return) {
		case SMB_QUERY_FILE_UNIX_BASIC:
			SSVAL(pdata,8,SMB_QUERY_FILE_UNIX_BASIC);
			SSVAL(pdata,10,0); /* Padding. */
			store_file_unix_basic(conn, pdata + 12, fsp, psbuf);
			break;
		case SMB_QUERY_FILE_UNIX_INFO2:
			SSVAL(pdata,8,SMB_QUERY_FILE_UNIX_INFO2);
			SSVAL(pdata,10,0); /* Padding. */
			store_file_unix_basic_info2(conn, pdata + 12, fsp, psbuf);
			break;
		default:
			SSVAL(pdata,8,SMB_NO_INFO_LEVEL_RETURNED);
			SSVAL(pdata,10,0); /* Padding. */
			break;
	}

	return status;
}

/****************************************************************************
 Open/Create a file with POSIX semantics.
****************************************************************************/

static NTSTATUS smb_posix_open(connection_struct *conn,
			       struct smb_request *req,
				char **ppdata,
				int total_data,
				const char *fname,
				SMB_STRUCT_STAT *psbuf,
				int *pdata_return_size)
{
	bool extended_oplock_granted = False;
	char *pdata = *ppdata;
	uint32 flags = 0;
	uint32 wire_open_mode = 0;
	uint32 raw_unixmode = 0;
	uint32 mod_unixmode = 0;
	uint32 create_disp = 0;
	uint32 access_mask = 0;
	uint32 create_options = 0;
	NTSTATUS status = NT_STATUS_OK;
	mode_t unixmode = (mode_t)0;
	files_struct *fsp = NULL;
	int oplock_request = 0;
	int info = 0;
	uint16 info_level_return = 0;

	if (total_data < 18) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	flags = IVAL(pdata,0);
	oplock_request = (flags & REQUEST_OPLOCK) ? EXCLUSIVE_OPLOCK : 0;
	if (oplock_request) {
		oplock_request |= (flags & REQUEST_BATCH_OPLOCK) ? BATCH_OPLOCK : 0;
	}

	wire_open_mode = IVAL(pdata,4);

	if (wire_open_mode == (SMB_O_CREAT|SMB_O_DIRECTORY)) {
		return smb_posix_mkdir(conn, req,
					ppdata,
					total_data,
					fname,
					psbuf,
					pdata_return_size);
	}

	switch (wire_open_mode & SMB_ACCMODE) {
		case SMB_O_RDONLY:
			access_mask = FILE_READ_DATA;
			break;
		case SMB_O_WRONLY:
			access_mask = FILE_WRITE_DATA;
			break;
		case SMB_O_RDWR:
			access_mask = FILE_READ_DATA|FILE_WRITE_DATA;
			break;
		default:
			DEBUG(5,("smb_posix_open: invalid open mode 0x%x\n",
				(unsigned int)wire_open_mode ));
			return NT_STATUS_INVALID_PARAMETER;
	}

	wire_open_mode &= ~SMB_ACCMODE;

	if((wire_open_mode & (SMB_O_CREAT | SMB_O_EXCL)) == (SMB_O_CREAT | SMB_O_EXCL)) {
		create_disp = FILE_CREATE;
	} else if((wire_open_mode & (SMB_O_CREAT | SMB_O_TRUNC)) == (SMB_O_CREAT | SMB_O_TRUNC)) {
		create_disp = FILE_OVERWRITE_IF;
	} else if((wire_open_mode & SMB_O_CREAT) == SMB_O_CREAT) {
		create_disp = FILE_OPEN_IF;
	} else if ((wire_open_mode & (SMB_O_CREAT | SMB_O_EXCL | SMB_O_TRUNC)) == 0) {
		create_disp = FILE_OPEN;
	} else {
		DEBUG(5,("smb_posix_open: invalid create mode 0x%x\n",
			(unsigned int)wire_open_mode ));
		return NT_STATUS_INVALID_PARAMETER;
	}

	raw_unixmode = IVAL(pdata,8);
	/* Next 4 bytes are not yet defined. */

	status = unix_perms_from_wire(conn,
				psbuf,
				raw_unixmode,
				VALID_STAT(*psbuf) ? PERM_EXISTING_FILE : PERM_NEW_FILE,
				&unixmode);

	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	mod_unixmode = (uint32)unixmode | FILE_FLAG_POSIX_SEMANTICS;

	if (wire_open_mode & SMB_O_SYNC) {
		create_options |= FILE_WRITE_THROUGH;
	}
	if (wire_open_mode & SMB_O_APPEND) {
		access_mask |= FILE_APPEND_DATA;
	}
	if (wire_open_mode & SMB_O_DIRECT) {
		mod_unixmode |= FILE_FLAG_NO_BUFFERING;
	}

	DEBUG(10,("smb_posix_open: file %s, smb_posix_flags = %u, mode 0%o\n",
		fname,
		(unsigned int)wire_open_mode,
		(unsigned int)unixmode ));

        status = SMB_VFS_CREATE_FILE(
		conn,					/* conn */
		req,					/* req */
		0,					/* root_dir_fid */
		fname,					/* fname */
		0,					/* create_file_flags */
		access_mask,				/* access_mask */
		(FILE_SHARE_READ | FILE_SHARE_WRITE |	/* share_access */
		    FILE_SHARE_DELETE),
		create_disp,				/* create_disposition*/
		FILE_NON_DIRECTORY_FILE,		/* create_options */
		mod_unixmode,				/* file_attributes */
		oplock_request,				/* oplock_request */
		0,					/* allocation_size */
		NULL,					/* sd */
		NULL,					/* ea_list */
		&fsp,					/* result */
		&info,					/* pinfo */
		psbuf);					/* psbuf */

	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	if (oplock_request && lp_fake_oplocks(SNUM(conn))) {
		extended_oplock_granted = True;
	}

	if(oplock_request && EXCLUSIVE_OPLOCK_TYPE(fsp->oplock_type)) {
		extended_oplock_granted = True;
	}

	info_level_return = SVAL(pdata,16);
 
	/* Allocate the correct return size. */

	if (info_level_return == SMB_QUERY_FILE_UNIX_BASIC) {
		*pdata_return_size = 12 + SMB_FILE_UNIX_BASIC_SIZE;
	} else if (info_level_return ==  SMB_QUERY_FILE_UNIX_INFO2) {
		*pdata_return_size = 12 + SMB_FILE_UNIX_INFO2_SIZE;
	} else {
		*pdata_return_size = 12;
	}

	/* Realloc the data size */
	*ppdata = (char *)SMB_REALLOC(*ppdata,*pdata_return_size);
	if (*ppdata == NULL) {
		close_file(req, fsp, ERROR_CLOSE);
		*pdata_return_size = 0;
		return NT_STATUS_NO_MEMORY;
	}
	pdata = *ppdata;

	if (extended_oplock_granted) {
		if (flags & REQUEST_BATCH_OPLOCK) {
			SSVAL(pdata,0, BATCH_OPLOCK_RETURN);
		} else {
			SSVAL(pdata,0, EXCLUSIVE_OPLOCK_RETURN);
		}
	} else if (fsp->oplock_type == LEVEL_II_OPLOCK) {
		SSVAL(pdata,0, LEVEL_II_OPLOCK_RETURN);
	} else {
		SSVAL(pdata,0,NO_OPLOCK_RETURN);
	}

	SSVAL(pdata,2,fsp->fnum);
	SIVAL(pdata,4,info); /* Was file created etc. */

	switch (info_level_return) {
		case SMB_QUERY_FILE_UNIX_BASIC:
			SSVAL(pdata,8,SMB_QUERY_FILE_UNIX_BASIC);
			SSVAL(pdata,10,0); /* padding. */
			store_file_unix_basic(conn, pdata + 12, fsp, psbuf);
			break;
		case SMB_QUERY_FILE_UNIX_INFO2:
			SSVAL(pdata,8,SMB_QUERY_FILE_UNIX_INFO2);
			SSVAL(pdata,10,0); /* padding. */
			store_file_unix_basic_info2(conn, pdata + 12, fsp, psbuf);
			break;
		default:
			SSVAL(pdata,8,SMB_NO_INFO_LEVEL_RETURNED);
			SSVAL(pdata,10,0); /* padding. */
			break;
	}
	return NT_STATUS_OK;
}

/****************************************************************************
 Delete a file with POSIX semantics.
****************************************************************************/

static NTSTATUS smb_posix_unlink(connection_struct *conn,
				 struct smb_request *req,
				const char *pdata,
				int total_data,
				const char *fname,
				SMB_STRUCT_STAT *psbuf)
{
	NTSTATUS status = NT_STATUS_OK;
	files_struct *fsp = NULL;
	uint16 flags = 0;
	char del = 1;
	int info = 0;
	int create_options = 0;
	int i;
	struct share_mode_lock *lck = NULL;

	if (total_data < 2) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	flags = SVAL(pdata,0);

	if (!VALID_STAT(*psbuf)) {
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	if ((flags == SMB_POSIX_UNLINK_DIRECTORY_TARGET) &&
			!VALID_STAT_OF_DIR(*psbuf)) {
		return NT_STATUS_NOT_A_DIRECTORY;
	}

	DEBUG(10,("smb_posix_unlink: %s %s\n",
		(flags == SMB_POSIX_UNLINK_DIRECTORY_TARGET) ? "directory" : "file",
		fname));

	if (VALID_STAT_OF_DIR(*psbuf)) {
		create_options |= FILE_DIRECTORY_FILE;
	}

        status = SMB_VFS_CREATE_FILE(
		conn,					/* conn */
		req,					/* req */
		0,					/* root_dir_fid */
		fname,					/* fname */
		0,					/* create_file_flags */
		DELETE_ACCESS,				/* access_mask */
		(FILE_SHARE_READ | FILE_SHARE_WRITE |	/* share_access */
		    FILE_SHARE_DELETE),
		FILE_OPEN,				/* create_disposition*/
		create_options,				/* create_options */
		FILE_FLAG_POSIX_SEMANTICS|0777,		/* file_attributes */
		0,					/* oplock_request */
		0,					/* allocation_size */
		NULL,					/* sd */
		NULL,					/* ea_list */
		&fsp,					/* result */
		&info,					/* pinfo */
		psbuf);					/* psbuf */

	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/*
	 * Don't lie to client. If we can't really delete due to
	 * non-POSIX opens return SHARING_VIOLATION.
	 */

	lck = get_share_mode_lock(talloc_tos(), fsp->file_id, NULL, NULL,
				  NULL);
	if (lck == NULL) {
		DEBUG(0, ("smb_posix_unlink: Could not get share mode "
			"lock for file %s\n", fsp->fsp_name));
		close_file(req, fsp, NORMAL_CLOSE);
		return NT_STATUS_INVALID_PARAMETER;
	}

	/*
	 * See if others still have the file open. If this is the case, then
	 * don't delete. If all opens are POSIX delete we can set the delete
	 * on close disposition.
	 */
	for (i=0; i<lck->num_share_modes; i++) {
		struct share_mode_entry *e = &lck->share_modes[i];
		if (is_valid_share_mode_entry(e)) {
			if (e->flags & SHARE_MODE_FLAG_POSIX_OPEN) {
				continue;
			}
			/* Fail with sharing violation. */
			close_file(req, fsp, NORMAL_CLOSE);
			TALLOC_FREE(lck);
			return NT_STATUS_SHARING_VIOLATION;
		}
	}

	/*
	 * Set the delete on close.
	 */
	status = smb_set_file_disposition_info(conn,
						&del,
						1,
						fsp,
						fname,
						psbuf);

	if (!NT_STATUS_IS_OK(status)) {
		close_file(req, fsp, NORMAL_CLOSE);
		TALLOC_FREE(lck);
		return status;
	}
	TALLOC_FREE(lck);
	return close_file(req, fsp, NORMAL_CLOSE);
}

/****************************************************************************
 Reply to a TRANS2_SETFILEINFO (set file info by fileid or pathname).
****************************************************************************/

static void call_trans2setfilepathinfo(connection_struct *conn,
				       struct smb_request *req,
				       unsigned int tran_call,
				       char **pparams, int total_params,
				       char **ppdata, int total_data,
				       unsigned int max_data_bytes)
{
	char *params = *pparams;
	char *pdata = *ppdata;
	uint16 info_level;
	SMB_STRUCT_STAT sbuf;
	char *fname = NULL;
	files_struct *fsp = NULL;
	NTSTATUS status = NT_STATUS_OK;
	int data_return_size = 0;
	TALLOC_CTX *ctx = talloc_tos();

	if (!params) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	ZERO_STRUCT(sbuf);

	if (tran_call == TRANSACT2_SETFILEINFO) {
		if (total_params < 4) {
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}

		fsp = file_fsp(req, SVAL(params,0));
		/* Basic check for non-null fsp. */
	        if (!check_fsp_open(conn, req, fsp)) {
			return;
		}
		info_level = SVAL(params,2);

		fname = talloc_strdup(talloc_tos(),fsp->fsp_name);
		if (!fname) {
			reply_nterror(req, NT_STATUS_NO_MEMORY);
			return;
		}

		if(fsp->is_directory || fsp->fh->fd == -1) {
			/*
			 * This is actually a SETFILEINFO on a directory
			 * handle (returned from an NT SMB). NT5.0 seems
			 * to do this call. JRA.
			 */
			if (INFO_LEVEL_IS_UNIX(info_level)) {
				/* Always do lstat for UNIX calls. */
				if (SMB_VFS_LSTAT(conn,fname,&sbuf)) {
					DEBUG(3,("call_trans2setfilepathinfo: SMB_VFS_LSTAT of %s failed (%s)\n",fname,strerror(errno)));
					reply_unixerror(req,ERRDOS,ERRbadpath);
					return;
				}
			} else {
				if (SMB_VFS_STAT(conn,fname,&sbuf) != 0) {
					DEBUG(3,("call_trans2setfilepathinfo: fileinfo of %s failed (%s)\n",fname,strerror(errno)));
					reply_unixerror(req,ERRDOS,ERRbadpath);
					return;
				}
			}
		} else if (fsp->print_file) {
			/*
			 * Doing a DELETE_ON_CLOSE should cancel a print job.
			 */
			if ((info_level == SMB_SET_FILE_DISPOSITION_INFO) && CVAL(pdata,0)) {
				fsp->fh->private_options |= FILE_DELETE_ON_CLOSE;

				DEBUG(3,("call_trans2setfilepathinfo: Cancelling print job (%s)\n", fsp->fsp_name ));

				SSVAL(params,0,0);
				send_trans2_replies(conn, req, params, 2,
						    *ppdata, 0,
						    max_data_bytes);
				return;
			} else {
				reply_unixerror(req, ERRDOS, ERRbadpath);
				return;
			}
		} else {
			/*
			 * Original code - this is an open file.
			 */
		        if (!check_fsp(conn, req, fsp)) {
				return;
			}

			if (SMB_VFS_FSTAT(fsp, &sbuf) != 0) {
				DEBUG(3,("call_trans2setfilepathinfo: fstat of fnum %d failed (%s)\n",fsp->fnum, strerror(errno)));
				reply_unixerror(req, ERRDOS, ERRbadfid);
				return;
			}
		}
	} else {
		/* set path info */
		if (total_params < 7) {
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}

		info_level = SVAL(params,0);
		srvstr_get_path(ctx, params, req->flags2, &fname, &params[6],
				total_params - 6, STR_TERMINATE,
				&status);
		if (!NT_STATUS_IS_OK(status)) {
			reply_nterror(req, status);
			return;
		}

		status = resolve_dfspath(ctx, conn,
					 req->flags2 & FLAGS2_DFS_PATHNAMES,
					 fname,
					 &fname);
		if (!NT_STATUS_IS_OK(status)) {
			if (NT_STATUS_EQUAL(status,NT_STATUS_PATH_NOT_COVERED)) {
				reply_botherror(req,
						NT_STATUS_PATH_NOT_COVERED,
						ERRSRV, ERRbadpath);
				return;
			}
			reply_nterror(req, status);
			return;
		}

		status = unix_convert(ctx, conn, fname, False,
				&fname, NULL, &sbuf);
		if (!NT_STATUS_IS_OK(status)) {
			reply_nterror(req, status);
			return;
		}

		status = check_name(conn, fname);
		if (!NT_STATUS_IS_OK(status)) {
			reply_nterror(req, status);
			return;
		}

		if (INFO_LEVEL_IS_UNIX(info_level)) {
			/*
			 * For CIFS UNIX extensions the target name may not exist.
			 */

			/* Always do lstat for UNIX calls. */
			SMB_VFS_LSTAT(conn,fname,&sbuf);

		} else if (!VALID_STAT(sbuf) && SMB_VFS_STAT(conn,fname,&sbuf)) {
			DEBUG(3,("call_trans2setfilepathinfo: SMB_VFS_STAT of %s failed (%s)\n",fname,strerror(errno)));
			reply_unixerror(req, ERRDOS, ERRbadpath);
			return;
		}
	}

	if (INFO_LEVEL_IS_UNIX(info_level) && !lp_unix_extensions()) {
		reply_nterror(req, NT_STATUS_INVALID_LEVEL);
		return;
	}

	if (!CAN_WRITE(conn)) {
		/* Allow POSIX opens. The open path will deny
		 * any non-readonly opens. */
		if (info_level != SMB_POSIX_PATH_OPEN) {
			reply_doserror(req, ERRSRV, ERRaccess);
			return;
		}
	}

	DEBUG(3,("call_trans2setfilepathinfo(%d) %s (fnum %d) info_level=%d totdata=%d\n",
		tran_call,fname, fsp ? fsp->fnum : -1, info_level,total_data));

	/* Realloc the parameter size */
	*pparams = (char *)SMB_REALLOC(*pparams,2);
	if (*pparams == NULL) {
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}
	params = *pparams;

	SSVAL(params,0,0);

	switch (info_level) {

		case SMB_INFO_STANDARD:
		{
			status = smb_set_info_standard(conn,
					pdata,
					total_data,
					fsp,
					fname,
					&sbuf);
			break;
		}

		case SMB_INFO_SET_EA:
		{
			status = smb_info_set_ea(conn,
						pdata,
						total_data,
						fsp,
						fname);
			break;
		}

		case SMB_SET_FILE_BASIC_INFO:
		case SMB_FILE_BASIC_INFORMATION:
		{
			status = smb_set_file_basic_info(conn,
							pdata,
							total_data,
							fsp,
							fname,
							&sbuf);
			break;
		}

		case SMB_FILE_ALLOCATION_INFORMATION:
		case SMB_SET_FILE_ALLOCATION_INFO:
		{
			status = smb_set_file_allocation_info(conn, req,
								pdata,
								total_data,
								fsp,
								fname,
								&sbuf);
			break;
		}

		case SMB_FILE_END_OF_FILE_INFORMATION:
		case SMB_SET_FILE_END_OF_FILE_INFO:
		{
			status = smb_set_file_end_of_file_info(conn, req,
								pdata,
								total_data,
								fsp,
								fname,
								&sbuf);
			break;
		}

		case SMB_FILE_DISPOSITION_INFORMATION:
		case SMB_SET_FILE_DISPOSITION_INFO: /* Set delete on close for open file. */
		{
#if 0
			/* JRA - We used to just ignore this on a path ? 
			 * Shouldn't this be invalid level on a pathname
			 * based call ?
			 */
			if (tran_call != TRANSACT2_SETFILEINFO) {
				return ERROR_NT(NT_STATUS_INVALID_LEVEL);
			}
#endif
			status = smb_set_file_disposition_info(conn,
						pdata,
						total_data,
						fsp,
						fname,
						&sbuf);
			break;
		}

		case SMB_FILE_POSITION_INFORMATION:
		{
			status = smb_file_position_information(conn,
						pdata,
						total_data,
						fsp);
			break;
		}

		/* From tridge Samba4 : 
		 * MODE_INFORMATION in setfileinfo (I have no
		 * idea what "mode information" on a file is - it takes a value of 0,
		 * 2, 4 or 6. What could it be?).
		 */

		case SMB_FILE_MODE_INFORMATION:
		{
			status = smb_file_mode_information(conn,
						pdata,
						total_data);
			break;
		}

		/*
		 * CIFS UNIX extensions.
		 */

		case SMB_SET_FILE_UNIX_BASIC:
		{
			status = smb_set_file_unix_basic(conn, req,
							pdata,
							total_data,
							fsp,
							fname,
							&sbuf);
			break;
		}

		case SMB_SET_FILE_UNIX_INFO2:
		{
			status = smb_set_file_unix_info2(conn, req,
							pdata,
							total_data,
							fsp,
							fname,
							&sbuf);
			break;
		}

		case SMB_SET_FILE_UNIX_LINK:
		{
			if (tran_call != TRANSACT2_SETPATHINFO) {
				/* We must have a pathname for this. */
				reply_nterror(req, NT_STATUS_INVALID_LEVEL);
				return;
			}
			status = smb_set_file_unix_link(conn, req, pdata,
							total_data, fname);
			break;
		}

		case SMB_SET_FILE_UNIX_HLINK:
		{
			if (tran_call != TRANSACT2_SETPATHINFO) {
				/* We must have a pathname for this. */
				reply_nterror(req, NT_STATUS_INVALID_LEVEL);
				return;
			}
			status = smb_set_file_unix_hlink(conn, req,
							 pdata,	total_data,
							 fname);
			break;
		}

		case SMB_FILE_RENAME_INFORMATION:
		{
			status = smb_file_rename_information(conn, req,
							     pdata, total_data,
							     fsp, fname);
			break;
		}

#if defined(HAVE_POSIX_ACLS)
		case SMB_SET_POSIX_ACL:
		{
			status = smb_set_posix_acl(conn,
						pdata,
						total_data,
						fsp,
						fname,
						&sbuf);
			break;
		}
#endif

		case SMB_SET_POSIX_LOCK:
		{
			if (tran_call != TRANSACT2_SETFILEINFO) {
				reply_nterror(req, NT_STATUS_INVALID_LEVEL);
				return;
			}
			status = smb_set_posix_lock(conn, req,
						    pdata, total_data, fsp);
			break;
		}

		case SMB_POSIX_PATH_OPEN:
		{
			if (tran_call != TRANSACT2_SETPATHINFO) {
				/* We must have a pathname for this. */
				reply_nterror(req, NT_STATUS_INVALID_LEVEL);
				return;
			}

			status = smb_posix_open(conn, req,
						ppdata,
						total_data,
						fname,
						&sbuf,
						&data_return_size);
			break;
		}

		case SMB_POSIX_PATH_UNLINK:
		{
			if (tran_call != TRANSACT2_SETPATHINFO) {
				/* We must have a pathname for this. */
				reply_nterror(req, NT_STATUS_INVALID_LEVEL);
				return;
			}

			status = smb_posix_unlink(conn, req,
						pdata,
						total_data,
						fname,
						&sbuf);
			break;
		}

		default:
			reply_nterror(req, NT_STATUS_INVALID_LEVEL);
			return;
	}

	
	if (!NT_STATUS_IS_OK(status)) {
		if (open_was_deferred(req->mid)) {
			/* We have re-scheduled this call. */
			return;
		}
		if (blocking_lock_was_deferred(req->mid)) {
			/* We have re-scheduled this call. */
			return;
		}
		if (NT_STATUS_EQUAL(status,NT_STATUS_PATH_NOT_COVERED)) {
			reply_botherror(req, NT_STATUS_PATH_NOT_COVERED,
					ERRSRV, ERRbadpath);
			return;
		}
		if (info_level == SMB_POSIX_PATH_OPEN) {
			reply_openerror(req, status);
			return;
		}

		reply_nterror(req, status);
		return;
	}

	SSVAL(params,0,0);
	send_trans2_replies(conn, req, params, 2, *ppdata, data_return_size,
			    max_data_bytes);
  
	return;
}

/****************************************************************************
 Reply to a TRANS2_MKDIR (make directory with extended attributes).
****************************************************************************/

static void call_trans2mkdir(connection_struct *conn, struct smb_request *req,
			     char **pparams, int total_params,
			     char **ppdata, int total_data,
			     unsigned int max_data_bytes)
{
	char *params = *pparams;
	char *pdata = *ppdata;
	char *directory = NULL;
	SMB_STRUCT_STAT sbuf;
	NTSTATUS status = NT_STATUS_OK;
	struct ea_list *ea_list = NULL;
	TALLOC_CTX *ctx = talloc_tos();

	if (!CAN_WRITE(conn)) {
		reply_doserror(req, ERRSRV, ERRaccess);
		return;
	}

	if (total_params < 5) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	srvstr_get_path(ctx, params, req->flags2, &directory, &params[4],
			total_params - 4, STR_TERMINATE,
			&status);
	if (!NT_STATUS_IS_OK(status)) {
		reply_nterror(req, status);
		return;
	}

	DEBUG(3,("call_trans2mkdir : name = %s\n", directory));

	status = resolve_dfspath(ctx,
				conn,
				req->flags2 & FLAGS2_DFS_PATHNAMES,
				directory,
				&directory);
	if (!NT_STATUS_IS_OK(status)) {
		if (NT_STATUS_EQUAL(status,NT_STATUS_PATH_NOT_COVERED)) {
			reply_botherror(req,
					NT_STATUS_PATH_NOT_COVERED,
					ERRSRV, ERRbadpath);
		}
		reply_nterror(req, status);
		return;
	}

	status = unix_convert(ctx, conn, directory, False, &directory, NULL, &sbuf);
	if (!NT_STATUS_IS_OK(status)) {
		reply_nterror(req, status);
		return;
	}

	status = check_name(conn, directory);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(5,("call_trans2mkdir error (%s)\n", nt_errstr(status)));
		reply_nterror(req, status);
		return;
	}

	/* Any data in this call is an EA list. */
	if (total_data && (total_data != 4) && !lp_ea_support(SNUM(conn))) {
		reply_nterror(req, NT_STATUS_EAS_NOT_SUPPORTED);
		return;
	}

	/*
	 * OS/2 workplace shell seems to send SET_EA requests of "null"
	 * length (4 bytes containing IVAL 4).
	 * They seem to have no effect. Bug #3212. JRA.
	 */

	if (total_data != 4) {
		if (total_data < 10) {
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}

		if (IVAL(pdata,0) > total_data) {
			DEBUG(10,("call_trans2mkdir: bad total data size (%u) > %u\n",
				IVAL(pdata,0), (unsigned int)total_data));
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}

		ea_list = read_ea_list(talloc_tos(), pdata + 4,
				       total_data - 4);
		if (!ea_list) {
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			return;
		}
	}
	/* If total_data == 4 Windows doesn't care what values
	 * are placed in that field, it just ignores them.
	 * The System i QNTC IBM SMB client puts bad values here,
	 * so ignore them. */

	status = create_directory(conn, req, directory);

	if (!NT_STATUS_IS_OK(status)) {
		reply_nterror(req, status);
		return;
	}
  
	/* Try and set any given EA. */
	if (ea_list) {
		status = set_ea(conn, NULL, directory, ea_list);
		if (!NT_STATUS_IS_OK(status)) {
			reply_nterror(req, status);
			return;
		}
	}

	/* Realloc the parameter and data sizes */
	*pparams = (char *)SMB_REALLOC(*pparams,2);
	if(*pparams == NULL) {
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}
	params = *pparams;

	SSVAL(params,0,0);

	send_trans2_replies(conn, req, params, 2, *ppdata, 0, max_data_bytes);
  
	return;
}

/****************************************************************************
 Reply to a TRANS2_FINDNOTIFYFIRST (start monitoring a directory for changes).
 We don't actually do this - we just send a null response.
****************************************************************************/

static void call_trans2findnotifyfirst(connection_struct *conn,
				       struct smb_request *req,
				       char **pparams, int total_params,
				       char **ppdata, int total_data,
				       unsigned int max_data_bytes)
{
	char *params = *pparams;
	uint16 info_level;

	if (total_params < 6) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	info_level = SVAL(params,4);
	DEBUG(3,("call_trans2findnotifyfirst - info_level %d\n", info_level));

	switch (info_level) {
		case 1:
		case 2:
			break;
		default:
			reply_nterror(req, NT_STATUS_INVALID_LEVEL);
			return;
	}

	/* Realloc the parameter and data sizes */
	*pparams = (char *)SMB_REALLOC(*pparams,6);
	if (*pparams == NULL) {
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}
	params = *pparams;

	SSVAL(params,0,fnf_handle);
	SSVAL(params,2,0); /* No changes */
	SSVAL(params,4,0); /* No EA errors */

	fnf_handle++;

	if(fnf_handle == 0)
		fnf_handle = 257;

	send_trans2_replies(conn, req, params, 6, *ppdata, 0, max_data_bytes);
  
	return;
}

/****************************************************************************
 Reply to a TRANS2_FINDNOTIFYNEXT (continue monitoring a directory for 
 changes). Currently this does nothing.
****************************************************************************/

static void call_trans2findnotifynext(connection_struct *conn,
				      struct smb_request *req,
				      char **pparams, int total_params,
				      char **ppdata, int total_data,
				      unsigned int max_data_bytes)
{
	char *params = *pparams;

	DEBUG(3,("call_trans2findnotifynext\n"));

	/* Realloc the parameter and data sizes */
	*pparams = (char *)SMB_REALLOC(*pparams,4);
	if (*pparams == NULL) {
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}
	params = *pparams;

	SSVAL(params,0,0); /* No changes */
	SSVAL(params,2,0); /* No EA errors */

	send_trans2_replies(conn, req, params, 4, *ppdata, 0, max_data_bytes);
  
	return;
}

/****************************************************************************
 Reply to a TRANS2_GET_DFS_REFERRAL - Shirish Kalele <kalele@veritas.com>.
****************************************************************************/

static void call_trans2getdfsreferral(connection_struct *conn,
				      struct smb_request *req,
				      char **pparams, int total_params,
				      char **ppdata, int total_data,
				      unsigned int max_data_bytes)
{
	char *params = *pparams;
  	char *pathname = NULL;
	int reply_size = 0;
	int max_referral_level;
	NTSTATUS status = NT_STATUS_OK;
	TALLOC_CTX *ctx = talloc_tos();

	DEBUG(10,("call_trans2getdfsreferral\n"));

	if (total_params < 3) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		return;
	}

	max_referral_level = SVAL(params,0);

	if(!lp_host_msdfs()) {
		reply_doserror(req, ERRDOS, ERRbadfunc);
		return;
	}

	srvstr_pull_talloc(ctx, params, req->flags2, &pathname, &params[2],
		    total_params - 2, STR_TERMINATE);
	if (!pathname) {
		reply_nterror(req, NT_STATUS_NOT_FOUND);
		return;
	}
	if((reply_size = setup_dfs_referral(conn, pathname, max_referral_level,
					    ppdata,&status)) < 0) {
		reply_nterror(req, status);
		return;
	}

	SSVAL(req->inbuf, smb_flg2,
	      SVAL(req->inbuf,smb_flg2) | FLAGS2_DFS_PATHNAMES);
	send_trans2_replies(conn, req,0,0,*ppdata,reply_size, max_data_bytes);

	return;
}

#define LMCAT_SPL       0x53
#define LMFUNC_GETJOBID 0x60

/****************************************************************************
 Reply to a TRANS2_IOCTL - used for OS/2 printing.
****************************************************************************/

static void call_trans2ioctl(connection_struct *conn,
			     struct smb_request *req,
			     char **pparams, int total_params,
			     char **ppdata, int total_data,
			     unsigned int max_data_bytes)
{
	char *pdata = *ppdata;
	files_struct *fsp = file_fsp(req, SVAL(req->vwv+15, 0));

	/* check for an invalid fid before proceeding */

	if (!fsp) {
		reply_doserror(req, ERRDOS, ERRbadfid);
		return;
	}

	if ((SVAL(req->vwv+16, 0) == LMCAT_SPL)
	    && (SVAL(req->vwv+17, 0) == LMFUNC_GETJOBID)) {
		*ppdata = (char *)SMB_REALLOC(*ppdata, 32);
		if (*ppdata == NULL) {
			reply_nterror(req, NT_STATUS_NO_MEMORY);
			return;
		}
		pdata = *ppdata;

		/* NOTE - THIS IS ASCII ONLY AT THE MOMENT - NOT SURE IF OS/2
			CAN ACCEPT THIS IN UNICODE. JRA. */

		SSVAL(pdata,0,fsp->rap_print_jobid);                     /* Job number */
		srvstr_push(pdata, req->flags2, pdata + 2,
			    global_myname(), 15,
			    STR_ASCII|STR_TERMINATE); /* Our NetBIOS name */
		srvstr_push(pdata, req->flags2, pdata+18,
			    lp_servicename(SNUM(conn)), 13,
			    STR_ASCII|STR_TERMINATE); /* Service name */
		send_trans2_replies(conn, req, *pparams, 0, *ppdata, 32,
				    max_data_bytes);
		return;
	}

	DEBUG(2,("Unknown TRANS2_IOCTL\n"));
	reply_doserror(req, ERRSRV, ERRerror);
}

/****************************************************************************
 Reply to a SMBfindclose (stop trans2 directory search).
****************************************************************************/

void reply_findclose(struct smb_request *req)
{
	int dptr_num;

	START_PROFILE(SMBfindclose);

	if (req->wct < 1) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		END_PROFILE(SMBfindclose);
		return;
	}

	dptr_num = SVALS(req->vwv+0, 0);

	DEBUG(3,("reply_findclose, dptr_num = %d\n", dptr_num));

	dptr_close(&dptr_num);

	reply_outbuf(req, 0, 0);

	DEBUG(3,("SMBfindclose dptr_num = %d\n", dptr_num));

	END_PROFILE(SMBfindclose);
	return;
}

/****************************************************************************
 Reply to a SMBfindnclose (stop FINDNOTIFYFIRST directory search).
****************************************************************************/

void reply_findnclose(struct smb_request *req)
{
	int dptr_num;

	START_PROFILE(SMBfindnclose);

	if (req->wct < 1) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		END_PROFILE(SMBfindnclose);
		return;
	}
	
	dptr_num = SVAL(req->vwv+0, 0);

	DEBUG(3,("reply_findnclose, dptr_num = %d\n", dptr_num));

	/* We never give out valid handles for a 
	   findnotifyfirst - so any dptr_num is ok here. 
	   Just ignore it. */

	reply_outbuf(req, 0, 0);

	DEBUG(3,("SMB_findnclose dptr_num = %d\n", dptr_num));

	END_PROFILE(SMBfindnclose);
	return;
}

static void handle_trans2(connection_struct *conn, struct smb_request *req,
			  struct trans_state *state)
{
	if (Protocol >= PROTOCOL_NT1) {
		req->flags2 |= 0x40; /* IS_LONG_NAME */
		SSVAL(req->inbuf,smb_flg2,req->flags2);
	}

	if (conn->encrypt_level == Required && !req->encrypted) {
		if (state->call != TRANSACT2_QFSINFO &&
				state->call != TRANSACT2_SETFSINFO) {
			DEBUG(0,("handle_trans2: encryption required "
				"with call 0x%x\n",
				(unsigned int)state->call));
			reply_nterror(req, NT_STATUS_ACCESS_DENIED);
			return;
		}
	}

	SMB_PERFCOUNT_SET_SUBOP(&req->pcd, state->call);

	/* Now we must call the relevant TRANS2 function */
	switch(state->call)  {
	case TRANSACT2_OPEN:
	{
		START_PROFILE(Trans2_open);
		call_trans2open(conn, req,
				&state->param, state->total_param,
				&state->data, state->total_data,
				state->max_data_return);
		END_PROFILE(Trans2_open);
		break;
	}

	case TRANSACT2_FINDFIRST:
	{
		START_PROFILE(Trans2_findfirst);
		call_trans2findfirst(conn, req,
				     &state->param, state->total_param,
				     &state->data, state->total_data,
				     state->max_data_return);
		END_PROFILE(Trans2_findfirst);
		break;
	}

	case TRANSACT2_FINDNEXT:
	{
		START_PROFILE(Trans2_findnext);
		call_trans2findnext(conn, req,
				    &state->param, state->total_param,
				    &state->data, state->total_data,
				    state->max_data_return);
		END_PROFILE(Trans2_findnext);
		break;
	}

	case TRANSACT2_QFSINFO:
	{
		START_PROFILE(Trans2_qfsinfo);
		call_trans2qfsinfo(conn, req,
				   &state->param, state->total_param,
				   &state->data, state->total_data,
				   state->max_data_return);
		END_PROFILE(Trans2_qfsinfo);
	    break;
	}

	case TRANSACT2_SETFSINFO:
	{
		START_PROFILE(Trans2_setfsinfo);
		call_trans2setfsinfo(conn, req,
				     &state->param, state->total_param,
				     &state->data, state->total_data,
				     state->max_data_return);
		END_PROFILE(Trans2_setfsinfo);
		break;
	}

	case TRANSACT2_QPATHINFO:
	case TRANSACT2_QFILEINFO:
	{
		START_PROFILE(Trans2_qpathinfo);
		call_trans2qfilepathinfo(conn, req, state->call,
					 &state->param, state->total_param,
					 &state->data, state->total_data,
					 state->max_data_return);
		END_PROFILE(Trans2_qpathinfo);
		break;
	}

	case TRANSACT2_SETPATHINFO:
	case TRANSACT2_SETFILEINFO:
	{
		START_PROFILE(Trans2_setpathinfo);
		call_trans2setfilepathinfo(conn, req, state->call,
					   &state->param, state->total_param,
					   &state->data, state->total_data,
					   state->max_data_return);
		END_PROFILE(Trans2_setpathinfo);
		break;
	}

	case TRANSACT2_FINDNOTIFYFIRST:
	{
		START_PROFILE(Trans2_findnotifyfirst);
		call_trans2findnotifyfirst(conn, req,
					   &state->param, state->total_param,
					   &state->data, state->total_data,
					   state->max_data_return);
		END_PROFILE(Trans2_findnotifyfirst);
		break;
	}

	case TRANSACT2_FINDNOTIFYNEXT:
	{
		START_PROFILE(Trans2_findnotifynext);
		call_trans2findnotifynext(conn, req,
					  &state->param, state->total_param,
					  &state->data, state->total_data,
					  state->max_data_return);
		END_PROFILE(Trans2_findnotifynext);
		break;
	}

	case TRANSACT2_MKDIR:
	{
		START_PROFILE(Trans2_mkdir);
		call_trans2mkdir(conn, req,
				 &state->param, state->total_param,
				 &state->data, state->total_data,
				 state->max_data_return);
		END_PROFILE(Trans2_mkdir);
		break;
	}

	case TRANSACT2_GET_DFS_REFERRAL:
	{
		START_PROFILE(Trans2_get_dfs_referral);
		call_trans2getdfsreferral(conn, req,
					  &state->param, state->total_param,
					  &state->data, state->total_data,
					  state->max_data_return);
		END_PROFILE(Trans2_get_dfs_referral);
		break;
	}

	case TRANSACT2_IOCTL:
	{
		START_PROFILE(Trans2_ioctl);
		call_trans2ioctl(conn, req,
				 &state->param, state->total_param,
				 &state->data, state->total_data,
				 state->max_data_return);
		END_PROFILE(Trans2_ioctl);
		break;
	}

	default:
		/* Error in request */
		DEBUG(2,("Unknown request %d in trans2 call\n", state->call));
		reply_doserror(req, ERRSRV,ERRerror);
	}
}

/****************************************************************************
 Reply to a SMBtrans2.
 ****************************************************************************/

void reply_trans2(struct smb_request *req)
{
	connection_struct *conn = req->conn;
	unsigned int dsoff;
	unsigned int dscnt;
	unsigned int psoff;
	unsigned int pscnt;
	unsigned int tran_call;
	struct trans_state *state;
	NTSTATUS result;

	START_PROFILE(SMBtrans2);

	if (req->wct < 14) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		END_PROFILE(SMBtrans2);
		return;
	}

	dsoff = SVAL(req->vwv+12, 0);
	dscnt = SVAL(req->vwv+11, 0);
	psoff = SVAL(req->vwv+10, 0);
	pscnt = SVAL(req->vwv+9, 0);
	tran_call = SVAL(req->vwv+14, 0);

	result = allow_new_trans(conn->pending_trans, req->mid);
	if (!NT_STATUS_IS_OK(result)) {
		DEBUG(2, ("Got invalid trans2 request: %s\n",
			  nt_errstr(result)));
		reply_nterror(req, result);
		END_PROFILE(SMBtrans2);
		return;
	}

	if (IS_IPC(conn)) {
		switch (tran_call) {
		/* List the allowed trans2 calls on IPC$ */
		case TRANSACT2_OPEN:
		case TRANSACT2_GET_DFS_REFERRAL:
		case TRANSACT2_QFILEINFO:
		case TRANSACT2_QFSINFO:
		case TRANSACT2_SETFSINFO:
			break;
		default:
			reply_doserror(req, ERRSRV, ERRaccess);
			END_PROFILE(SMBtrans2);
			return;
		}
	}

	if ((state = TALLOC_P(conn, struct trans_state)) == NULL) {
		DEBUG(0, ("talloc failed\n"));
		reply_nterror(req, NT_STATUS_NO_MEMORY);
		END_PROFILE(SMBtrans2);
		return;
	}

	state->cmd = SMBtrans2;

	state->mid = req->mid;
	state->vuid = req->vuid;
	state->setup_count = SVAL(req->vwv+13, 0);
	state->setup = NULL;
	state->total_param = SVAL(req->vwv+0, 0);
	state->param = NULL;
	state->total_data =  SVAL(req->vwv+1, 0);
	state->data = NULL;
	state->max_param_return = SVAL(req->vwv+2, 0);
	state->max_data_return  = SVAL(req->vwv+3, 0);
	state->max_setup_return = SVAL(req->vwv+4, 0);
	state->close_on_completion = BITSETW(req->vwv+5, 0);
	state->one_way = BITSETW(req->vwv+5, 1);

	state->call = tran_call;

	/* All trans2 messages we handle have smb_sucnt == 1 - ensure this
	   is so as a sanity check */
	if (state->setup_count != 1) {
		/*
		 * Need to have rc=0 for ioctl to get job id for OS/2.
		 *  Network printing will fail if function is not successful.
		 *  Similar function in reply.c will be used if protocol
		 *  is LANMAN1.0 instead of LM1.2X002.
		 *  Until DosPrintSetJobInfo with PRJINFO3 is supported,
		 *  outbuf doesn't have to be set(only job id is used).
		 */
		if ( (state->setup_count == 4)
		     && (tran_call == TRANSACT2_IOCTL)
		     && (SVAL(req->vwv+16, 0) == LMCAT_SPL)
		     &&	(SVAL(req->vwv+17, 0) == LMFUNC_GETJOBID)) {
			DEBUG(2,("Got Trans2 DevIOctl jobid\n"));
		} else {
			DEBUG(2,("Invalid smb_sucnt in trans2 call(%u)\n",state->setup_count));
			DEBUG(2,("Transaction is %d\n",tran_call));
			TALLOC_FREE(state);
			reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
			END_PROFILE(SMBtrans2);
			return;
		}
	}

	if ((dscnt > state->total_data) || (pscnt > state->total_param))
		goto bad_param;

	if (state->total_data) {

		if (trans_oob(state->total_data, 0, dscnt)
		    || trans_oob(smb_len(req->inbuf), dsoff, dscnt)) {
			goto bad_param;
		}

		/* Can't use talloc here, the core routines do realloc on the
		 * params and data. */
		state->data = (char *)SMB_MALLOC(state->total_data);
		if (state->data == NULL) {
			DEBUG(0,("reply_trans2: data malloc fail for %u "
				 "bytes !\n", (unsigned int)state->total_data));
			TALLOC_FREE(state);
			reply_nterror(req, NT_STATUS_NO_MEMORY);
			END_PROFILE(SMBtrans2);
			return;
		}

		memcpy(state->data,smb_base(req->inbuf)+dsoff,dscnt);
	}

	if (state->total_param) {

		if (trans_oob(state->total_param, 0, pscnt)
		    || trans_oob(smb_len(req->inbuf), psoff, pscnt)) {
			goto bad_param;
		}

		/* Can't use talloc here, the core routines do realloc on the
		 * params and data. */
		state->param = (char *)SMB_MALLOC(state->total_param);
		if (state->param == NULL) {
			DEBUG(0,("reply_trans: param malloc fail for %u "
				 "bytes !\n", (unsigned int)state->total_param));
			SAFE_FREE(state->data);
			TALLOC_FREE(state);
			reply_nterror(req, NT_STATUS_NO_MEMORY);
			END_PROFILE(SMBtrans2);
			return;
		} 

		memcpy(state->param,smb_base(req->inbuf)+psoff,pscnt);
	}

	state->received_data  = dscnt;
	state->received_param = pscnt;

	if ((state->received_param == state->total_param) &&
	    (state->received_data == state->total_data)) {

		handle_trans2(conn, req, state);

		SAFE_FREE(state->data);
		SAFE_FREE(state->param);
		TALLOC_FREE(state);
		END_PROFILE(SMBtrans2);
		return;
	}

	DLIST_ADD(conn->pending_trans, state);

	/* We need to send an interim response then receive the rest
	   of the parameter/data bytes */
	reply_outbuf(req, 0, 0);
	show_msg((char *)req->outbuf);
	END_PROFILE(SMBtrans2);
	return;

  bad_param:

	DEBUG(0,("reply_trans2: invalid trans parameters\n"));
	SAFE_FREE(state->data);
	SAFE_FREE(state->param);
	TALLOC_FREE(state);
	END_PROFILE(SMBtrans2);
	reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
}


/****************************************************************************
 Reply to a SMBtranss2
 ****************************************************************************/

void reply_transs2(struct smb_request *req)
{
	connection_struct *conn = req->conn;
	unsigned int pcnt,poff,dcnt,doff,pdisp,ddisp;
	struct trans_state *state;

	START_PROFILE(SMBtranss2);

	show_msg((char *)req->inbuf);

	if (req->wct < 8) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		END_PROFILE(SMBtranss2);
		return;
	}

	for (state = conn->pending_trans; state != NULL;
	     state = state->next) {
		if (state->mid == req->mid) {
			break;
		}
	}

	if ((state == NULL) || (state->cmd != SMBtrans2)) {
		reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
		END_PROFILE(SMBtranss2);
		return;
	}

	/* Revise state->total_param and state->total_data in case they have
	   changed downwards */

	if (SVAL(req->vwv+0, 0) < state->total_param)
		state->total_param = SVAL(req->vwv+0, 0);
	if (SVAL(req->vwv+1, 0) < state->total_data)
		state->total_data = SVAL(req->vwv+1, 0);

	pcnt = SVAL(req->vwv+2, 0);
	poff = SVAL(req->vwv+3, 0);
	pdisp = SVAL(req->vwv+4, 0);

	dcnt = SVAL(req->vwv+5, 0);
	doff = SVAL(req->vwv+6, 0);
	ddisp = SVAL(req->vwv+7, 0);

	state->received_param += pcnt;
	state->received_data += dcnt;
		
	if ((state->received_data > state->total_data) ||
	    (state->received_param > state->total_param))
		goto bad_param;

	if (pcnt) {
		if (trans_oob(state->total_param, pdisp, pcnt)
		    || trans_oob(smb_len(req->inbuf), poff, pcnt)) {
			goto bad_param;
		}
		memcpy(state->param+pdisp,smb_base(req->inbuf)+poff,pcnt);
	}

	if (dcnt) {
		if (trans_oob(state->total_data, ddisp, dcnt)
		    || trans_oob(smb_len(req->inbuf), doff, dcnt)) {
			goto bad_param;
		}
		memcpy(state->data+ddisp, smb_base(req->inbuf)+doff,dcnt);
	}

	if ((state->received_param < state->total_param) ||
	    (state->received_data < state->total_data)) {
		END_PROFILE(SMBtranss2);
		return;
	}

	handle_trans2(conn, req, state);

	DLIST_REMOVE(conn->pending_trans, state);
	SAFE_FREE(state->data);
	SAFE_FREE(state->param);
	TALLOC_FREE(state);

	END_PROFILE(SMBtranss2);
	return;

  bad_param:

	DEBUG(0,("reply_transs2: invalid trans parameters\n"));
	DLIST_REMOVE(conn->pending_trans, state);
	SAFE_FREE(state->data);
	SAFE_FREE(state->param);
	TALLOC_FREE(state);
	reply_nterror(req, NT_STATUS_INVALID_PARAMETER);
	END_PROFILE(SMBtranss2);
	return;
}
