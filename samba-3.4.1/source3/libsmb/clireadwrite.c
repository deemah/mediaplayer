/*
   Unix SMB/CIFS implementation.
   client file read/write routines
   Copyright (C) Andrew Tridgell 1994-1998

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

/****************************************************************************
  Calculate the recommended read buffer size
****************************************************************************/
static size_t cli_read_max_bufsize(struct cli_state *cli)
{
	if (!client_is_signing_on(cli) && !cli_encryption_on(cli)
	    && (cli->posix_capabilities & CIFS_UNIX_LARGE_READ_CAP)) {
		return CLI_SAMBA_MAX_POSIX_LARGE_READX_SIZE;
	}
	if (cli->capabilities & CAP_LARGE_READX) {
		return cli->is_samba
			? CLI_SAMBA_MAX_LARGE_READX_SIZE
			: CLI_WINDOWS_MAX_LARGE_READX_SIZE;
	}
	return (cli->max_xmit - (smb_size+32)) & ~1023;
}

/****************************************************************************
  Calculate the recommended write buffer size
****************************************************************************/
static size_t cli_write_max_bufsize(struct cli_state *cli, uint16_t write_mode)
{
        if (write_mode == 0 &&
	    !client_is_signing_on(cli) &&
	    !cli_encryption_on(cli) &&
	    (cli->posix_capabilities & CIFS_UNIX_LARGE_WRITE_CAP) &&
	    (cli->capabilities & CAP_LARGE_FILES)) {
		/* Only do massive writes if we can do them direct
		 * with no signing or encrypting - not on a pipe. */
		return CLI_SAMBA_MAX_POSIX_LARGE_WRITEX_SIZE;
	}

	if (cli->is_samba) {
		return CLI_SAMBA_MAX_LARGE_WRITEX_SIZE;
	}

	if (((cli->capabilities & CAP_LARGE_WRITEX) == 0)
	    || client_is_signing_on(cli)
	    || strequal(cli->dev, "LPT1:")) {

		/*
		 * Printer devices are restricted to max_xmit writesize in
		 * Vista and XPSP3 as are signing connections.
		 */

		return (cli->max_xmit - (smb_size+32)) & ~1023;
	}

	return CLI_WINDOWS_MAX_LARGE_WRITEX_SIZE;
}


/*
 * Send a read&x request
 */

struct async_req *cli_read_andx_send(TALLOC_CTX *mem_ctx,
				     struct event_context *ev,
				     struct cli_state *cli, int fnum,
				     off_t offset, size_t size)
{
	struct async_req *result;
	struct cli_request *req;
	bool bigoffset = False;

	uint16_t vwv[12];
	uint8_t wct = 10;

	if (size > cli_read_max_bufsize(cli)) {
		DEBUG(0, ("cli_read_andx_send got size=%d, can only handle "
			  "size=%d\n", (int)size,
			  (int)cli_read_max_bufsize(cli)));
		return NULL;
	}

	SCVAL(vwv + 0, 0, 0xFF);
	SCVAL(vwv + 0, 1, 0);
	SSVAL(vwv + 1, 0, 0);
	SSVAL(vwv + 2, 0, fnum);
	SIVAL(vwv + 3, 0, offset);
	SSVAL(vwv + 5, 0, size);
	SSVAL(vwv + 6, 0, size);
	SSVAL(vwv + 7, 0, (size >> 16));
	SSVAL(vwv + 8, 0, 0);
	SSVAL(vwv + 9, 0, 0);

	if ((uint64_t)offset >> 32) {
		bigoffset = True;
		SIVAL(vwv + 10, 0,
		      (((uint64_t)offset)>>32) & 0xffffffff);
		wct += 2;
	}

	result = cli_request_send(mem_ctx, ev, cli, SMBreadX, 0, wct, vwv, 0,
				  0, NULL);
	if (result == NULL) {
		return NULL;
	}

	req = talloc_get_type_abort(result->private_data, struct cli_request);

	req->data.read.ofs = offset;
	req->data.read.size = size;
	req->data.read.received = 0;
	req->data.read.rcvbuf = NULL;

	return result;
}

/*
 * Pull the data out of a finished async read_and_x request. rcvbuf is
 * talloced from the request, so better make sure that you copy it away before
 * you talloc_free(req). "rcvbuf" is NOT a talloc_ctx of its own, so do not
 * talloc_move it!
 */

NTSTATUS cli_read_andx_recv(struct async_req *req, ssize_t *received,
			    uint8_t **rcvbuf)
{
	struct cli_request *cli_req = talloc_get_type_abort(
		req->private_data, struct cli_request);
	uint8_t wct;
	uint16_t *vwv;
	uint16_t num_bytes;
	uint8_t *bytes;
	uint8_t *buf;
	NTSTATUS status;
	size_t size;

	if (async_req_is_nterror(req, &status)) {
		return status;
	}

	status = cli_pull_reply(req, &wct, &vwv, &num_bytes, &bytes);

	if (NT_STATUS_IS_ERR(status)) {
		return status;
	}

	if (wct < 12) {
		return NT_STATUS_INVALID_NETWORK_RESPONSE;
	}

	/* size is the number of bytes the server returned.
	 * Might be zero. */
	size = SVAL(vwv + 5, 0);
	size |= (((unsigned int)SVAL(vwv + 7, 0)) << 16);

	if (size > cli_req->data.read.size) {
		DEBUG(5,("server returned more than we wanted!\n"));
		return NT_STATUS_UNEXPECTED_IO_ERROR;
	}

	/*
	 * bcc field must be valid for small reads, for large reads the 16-bit
	 * bcc field can't be correct.
	 */

	if ((size < 0xffff) && (size > num_bytes)) {
		DEBUG(5, ("server announced more bytes than sent\n"));
		return NT_STATUS_INVALID_NETWORK_RESPONSE;
	}

	buf = (uint8_t *)smb_base(cli_req->inbuf) + SVAL(vwv+6, 0);

	if (trans_oob(smb_len(cli_req->inbuf), SVAL(vwv+6, 0), size)
	    || (size && (buf < bytes))) {
		DEBUG(5, ("server returned invalid read&x data offset\n"));
		return NT_STATUS_INVALID_NETWORK_RESPONSE;
	}

	*rcvbuf = (uint8_t *)(smb_base(cli_req->inbuf) + SVAL(vwv + 6, 0));
	*received = size;
	return NT_STATUS_OK;
}

/*
 * Parallel read support.
 *
 * cli_pull sends as many read&x requests as the server would allow via
 * max_mux at a time. When replies flow back in, the data is written into
 * the callback function "sink" in the right order.
 */

struct cli_pull_state {
	struct async_req *req;

	struct event_context *ev;
	struct cli_state *cli;
	uint16_t fnum;
	off_t start_offset;
	SMB_OFF_T size;

	NTSTATUS (*sink)(char *buf, size_t n, void *priv);
	void *priv;

	size_t chunk_size;

	/*
	 * Outstanding requests
	 */
	int num_reqs;
	struct async_req **reqs;

	/*
	 * For how many bytes did we send requests already?
	 */
	SMB_OFF_T requested;

	/*
	 * Next request index to push into "sink". This walks around the "req"
	 * array, taking care that the requests are pushed to "sink" in the
	 * right order. If necessary (i.e. replies don't come in in the right
	 * order), replies are held back in "reqs".
	 */
	int top_req;

	/*
	 * How many bytes did we push into "sink"?
	 */

	SMB_OFF_T pushed;
};

static char *cli_pull_print(TALLOC_CTX *mem_ctx, struct async_req *req)
{
	struct cli_pull_state *state = talloc_get_type_abort(
		req->private_data, struct cli_pull_state);
	char *result;

	result = async_req_print(mem_ctx, req);
	if (result == NULL) {
		return NULL;
	}

	return talloc_asprintf_append_buffer(
		result, "num_reqs=%d, top_req=%d",
		state->num_reqs, state->top_req);
}

static void cli_pull_read_done(struct async_req *read_req);

/*
 * Prepare an async pull request
 */

struct async_req *cli_pull_send(TALLOC_CTX *mem_ctx,
				struct event_context *ev,
				struct cli_state *cli,
				uint16_t fnum, off_t start_offset,
				SMB_OFF_T size, size_t window_size,
				NTSTATUS (*sink)(char *buf, size_t n,
						 void *priv),
				void *priv)
{
	struct async_req *result;
	struct cli_pull_state *state;
	int i;

	if (!async_req_setup(mem_ctx, &result, &state,
			     struct cli_pull_state)) {
		return NULL;
	}
	result->print = cli_pull_print;
	state->req = result;

	state->cli = cli;
	state->ev = ev;
	state->fnum = fnum;
	state->start_offset = start_offset;
	state->size = size;
	state->sink = sink;
	state->priv = priv;

	state->pushed = 0;
	state->top_req = 0;

	if (size == 0) {
		if (!async_post_ntstatus(result, ev, NT_STATUS_OK)) {
			goto failed;
		}
		return result;
	}

	state->chunk_size = cli_read_max_bufsize(cli);

	state->num_reqs = MAX(window_size/state->chunk_size, 1);
	state->num_reqs = MIN(state->num_reqs, cli->max_mux);

	state->reqs = TALLOC_ZERO_ARRAY(state, struct async_req *,
					state->num_reqs);
	if (state->reqs == NULL) {
		goto failed;
	}

	state->requested = 0;

	for (i=0; i<state->num_reqs; i++) {
		SMB_OFF_T size_left;
		size_t request_thistime;

		if (state->requested >= size) {
			state->num_reqs = i;
			break;
		}

		size_left = size - state->requested;
		request_thistime = MIN(size_left, state->chunk_size);

		state->reqs[i] = cli_read_andx_send(
			state->reqs, ev, cli, fnum,
			state->start_offset + state->requested,
			request_thistime);

		if (state->reqs[i] == NULL) {
			goto failed;
		}

		state->reqs[i]->async.fn = cli_pull_read_done;
		state->reqs[i]->async.priv = result;

		state->requested += request_thistime;
	}
	return result;

failed:
	TALLOC_FREE(result);
	return NULL;
}

/*
 * Handle incoming read replies, push the data into sink and send out new
 * requests if necessary.
 */

static void cli_pull_read_done(struct async_req *read_req)
{
	struct async_req *pull_req = talloc_get_type_abort(
		read_req->async.priv, struct async_req);
	struct cli_pull_state *state = talloc_get_type_abort(
		pull_req->private_data, struct cli_pull_state);
	struct cli_request *read_state = talloc_get_type_abort(
		read_req->private_data, struct cli_request);
	NTSTATUS status;

	status = cli_read_andx_recv(read_req, &read_state->data.read.received,
				    &read_state->data.read.rcvbuf);
	if (!NT_STATUS_IS_OK(status)) {
		async_req_nterror(state->req, status);
		return;
	}

	/*
	 * This loop is the one to take care of out-of-order replies. All
	 * pending requests are in state->reqs, state->reqs[top_req] is the
	 * one that is to be pushed next. If however a request later than
	 * top_req is replied to, then we can't push yet. If top_req is
	 * replied to at a later point then, we need to push all the finished
	 * requests.
	 */

	while (state->reqs[state->top_req] != NULL) {
		struct cli_request *top_read;

		DEBUG(11, ("cli_pull_read_done: top_req = %d\n",
			   state->top_req));

		if (state->reqs[state->top_req]->state < ASYNC_REQ_DONE) {
			DEBUG(11, ("cli_pull_read_done: top request not yet "
				   "done\n"));
			return;
		}

		top_read = talloc_get_type_abort(
			state->reqs[state->top_req]->private_data,
			struct cli_request);

		DEBUG(10, ("cli_pull_read_done: Pushing %d bytes, %d already "
			   "pushed\n", (int)top_read->data.read.received,
			   (int)state->pushed));

		status = state->sink((char *)top_read->data.read.rcvbuf,
				     top_read->data.read.received,
				     state->priv);
		if (!NT_STATUS_IS_OK(status)) {
			async_req_nterror(state->req, status);
			return;
		}
		state->pushed += top_read->data.read.received;

		TALLOC_FREE(state->reqs[state->top_req]);

		if (state->requested < state->size) {
			struct async_req *new_req;
			SMB_OFF_T size_left;
			size_t request_thistime;

			size_left = state->size - state->requested;
			request_thistime = MIN(size_left, state->chunk_size);

			DEBUG(10, ("cli_pull_read_done: Requesting %d bytes "
				   "at %d, position %d\n",
				   (int)request_thistime,
				   (int)(state->start_offset
					 + state->requested),
				   state->top_req));

			new_req = cli_read_andx_send(
				state->reqs, state->ev, state->cli,
				state->fnum,
				state->start_offset + state->requested,
				request_thistime);

			if (async_req_nomem(new_req, state->req)) {
				return;
			}

			new_req->async.fn = cli_pull_read_done;
			new_req->async.priv = pull_req;

			state->reqs[state->top_req] = new_req;
			state->requested += request_thistime;
		}

		state->top_req = (state->top_req+1) % state->num_reqs;
	}

	async_req_done(pull_req);
}

NTSTATUS cli_pull_recv(struct async_req *req, SMB_OFF_T *received)
{
	struct cli_pull_state *state = talloc_get_type_abort(
		req->private_data, struct cli_pull_state);
	NTSTATUS status;

	if (async_req_is_nterror(req, &status)) {
		return status;
	}
	*received = state->pushed;
	return NT_STATUS_OK;
}

NTSTATUS cli_pull(struct cli_state *cli, uint16_t fnum,
		  off_t start_offset, SMB_OFF_T size, size_t window_size,
		  NTSTATUS (*sink)(char *buf, size_t n, void *priv),
		  void *priv, SMB_OFF_T *received)
{
	TALLOC_CTX *frame = talloc_stackframe();
	struct event_context *ev;
	struct async_req *req;
	NTSTATUS result = NT_STATUS_NO_MEMORY;

	if (cli->fd_event != NULL) {
		/*
		 * Can't use sync call while an async call is in flight
		 */
		return NT_STATUS_INVALID_PARAMETER;
	}

	ev = event_context_init(frame);
	if (ev == NULL) {
		goto nomem;
	}

	req = cli_pull_send(frame, ev, cli, fnum, start_offset, size,
			    window_size, sink, priv);
	if (req == NULL) {
		goto nomem;
	}

	while (req->state < ASYNC_REQ_DONE) {
		event_loop_once(ev);
	}

	result = cli_pull_recv(req, received);
 nomem:
	TALLOC_FREE(frame);
	return result;
}

static NTSTATUS cli_read_sink(char *buf, size_t n, void *priv)
{
	char **pbuf = (char **)priv;
	memcpy(*pbuf, buf, n);
	*pbuf += n;
	return NT_STATUS_OK;
}

ssize_t cli_read(struct cli_state *cli, int fnum, char *buf,
		 off_t offset, size_t size)
{
	NTSTATUS status;
	SMB_OFF_T ret;

	status = cli_pull(cli, fnum, offset, size, size,
			  cli_read_sink, &buf, &ret);
	if (!NT_STATUS_IS_OK(status)) {
		cli_set_error(cli, status);
		return -1;
	}
	return ret;
}

/****************************************************************************
 Issue a single SMBwrite and don't wait for a reply.
****************************************************************************/

static bool cli_issue_write(struct cli_state *cli,
				int fnum,
				off_t offset,
				uint16 mode,
				const char *buf,
				size_t size,
				int i)
{
	char *p;
	bool large_writex = false;
	/* We can only do direct writes if not signing and not encrypting. */
	bool direct_writes = !client_is_signing_on(cli) && !cli_encryption_on(cli);

	if (!direct_writes && size + 1 > cli->bufsize) {
		cli->outbuf = (char *)SMB_REALLOC(cli->outbuf, size + 1024);
		if (!cli->outbuf) {
			return False;
		}
		cli->inbuf = (char *)SMB_REALLOC(cli->inbuf, size + 1024);
		if (cli->inbuf == NULL) {
			SAFE_FREE(cli->outbuf);
			return False;
		}
		cli->bufsize = size + 1024;
	}

	memset(cli->outbuf,'\0',smb_size);
	memset(cli->inbuf,'\0',smb_size);

	if (cli->capabilities & CAP_LARGE_FILES) {
		large_writex = True;
	}

	if (large_writex) {
		cli_set_message(cli->outbuf,14,0,True);
	} else {
		cli_set_message(cli->outbuf,12,0,True);
	}

	SCVAL(cli->outbuf,smb_com,SMBwriteX);
	SSVAL(cli->outbuf,smb_tid,cli->cnum);
	cli_setup_packet(cli);

	SCVAL(cli->outbuf,smb_vwv0,0xFF);
	SSVAL(cli->outbuf,smb_vwv2,fnum);

	SIVAL(cli->outbuf,smb_vwv3,offset);
	SIVAL(cli->outbuf,smb_vwv5,0);
	SSVAL(cli->outbuf,smb_vwv7,mode);

	SSVAL(cli->outbuf,smb_vwv8,(mode & 0x0008) ? size : 0);
	/*
	 * According to CIFS-TR-1p00, this following field should only
	 * be set if CAP_LARGE_WRITEX is set. We should check this
	 * locally. However, this check might already have been
	 * done by our callers.
	 */
	SSVAL(cli->outbuf,smb_vwv9,(size>>16));
	SSVAL(cli->outbuf,smb_vwv10,size);
	/* +1 is pad byte. */
	SSVAL(cli->outbuf,smb_vwv11,
	      smb_buf(cli->outbuf) - smb_base(cli->outbuf) + 1);

	if (large_writex) {
		SIVAL(cli->outbuf,smb_vwv12,(((uint64_t)offset)>>32) & 0xffffffff);
	}

	p = smb_base(cli->outbuf) + SVAL(cli->outbuf,smb_vwv11) -1;
	*p++ = '\0'; /* pad byte. */
	if (!direct_writes) {
		memcpy(p, buf, size);
	}
	if (size > 0x1FFFF) {
		/* This is a POSIX 14 word large write. */
		set_message_bcc(cli->outbuf, 0); /* Set bcc to zero. */
		_smb_setlen_large(cli->outbuf,smb_size + 28 + 1 /* pad */ + size - 4);
	} else {
		cli_setup_bcc(cli, p+size);
	}

	SSVAL(cli->outbuf,smb_mid,cli->mid + i);

	show_msg(cli->outbuf);
	if (direct_writes) {
		/* For direct writes we now need to write the data
		 * directly out of buf. */
		return cli_send_smb_direct_writeX(cli, buf, size);
	} else {
		return cli_send_smb(cli);
	}
}

/****************************************************************************
  write to a file
  write_mode: 0x0001 disallow write cacheing
              0x0002 return bytes remaining
              0x0004 use raw named pipe protocol
              0x0008 start of message mode named pipe protocol
****************************************************************************/

ssize_t cli_write(struct cli_state *cli,
    	         int fnum, uint16 write_mode,
		 const char *buf, off_t offset, size_t size)
{
	ssize_t bwritten = 0;
	unsigned int issued = 0;
	unsigned int received = 0;
	int mpx = 1;
	size_t writesize;
	int blocks;

	if(cli->max_mux > 1) {
		mpx = cli->max_mux-1;
	} else {
		mpx = 1;
	}

	writesize = cli_write_max_bufsize(cli, write_mode);

	blocks = (size + (writesize-1)) / writesize;

	while (received < blocks) {

		while ((issued - received < mpx) && (issued < blocks)) {
			ssize_t bsent = issued * writesize;
			ssize_t size1 = MIN(writesize, size - bsent);

			if (!cli_issue_write(cli, fnum, offset + bsent,
			                write_mode,
			                buf + bsent,
					size1, issued))
				return -1;
			issued++;
		}

		if (!cli_receive_smb(cli)) {
			return bwritten;
		}

		received++;

		if (cli_is_error(cli))
			break;

		bwritten += SVAL(cli->inbuf, smb_vwv2);
		if (writesize > 0xFFFF) {
			bwritten += (((int)(SVAL(cli->inbuf, smb_vwv4)))<<16);
		}
	}

	while (received < issued && cli_receive_smb(cli)) {
		received++;
	}

	return bwritten;
}

/****************************************************************************
  write to a file using a SMBwrite and not bypassing 0 byte writes
****************************************************************************/

ssize_t cli_smbwrite(struct cli_state *cli,
		     int fnum, char *buf, off_t offset, size_t size1)
{
	char *p;
	ssize_t total = 0;

	do {
		size_t size = MIN(size1, cli->max_xmit - 48);

		memset(cli->outbuf,'\0',smb_size);
		memset(cli->inbuf,'\0',smb_size);

		cli_set_message(cli->outbuf,5, 0,True);

		SCVAL(cli->outbuf,smb_com,SMBwrite);
		SSVAL(cli->outbuf,smb_tid,cli->cnum);
		cli_setup_packet(cli);

		SSVAL(cli->outbuf,smb_vwv0,fnum);
		SSVAL(cli->outbuf,smb_vwv1,size);
		SIVAL(cli->outbuf,smb_vwv2,offset);
		SSVAL(cli->outbuf,smb_vwv4,0);

		p = smb_buf(cli->outbuf);
		*p++ = 1;
		SSVAL(p, 0, size); p += 2;
		memcpy(p, buf + total, size); p += size;

		cli_setup_bcc(cli, p);

		if (!cli_send_smb(cli))
			return -1;

		if (!cli_receive_smb(cli))
			return -1;

		if (cli_is_error(cli))
			return -1;

		size = SVAL(cli->inbuf,smb_vwv0);
		if (size == 0)
			break;

		size1 -= size;
		total += size;
		offset += size;

	} while (size1);

	return total;
}

/*
 * Send a write&x request
 */

struct async_req *cli_write_andx_send(TALLOC_CTX *mem_ctx,
				      struct event_context *ev,
				      struct cli_state *cli, uint16_t fnum,
				      uint16_t mode, const uint8_t *buf,
				      off_t offset, size_t size)
{
	bool bigoffset = ((cli->capabilities & CAP_LARGE_FILES) != 0);
	uint8_t wct = bigoffset ? 14 : 12;
	size_t max_write = cli_write_max_bufsize(cli, mode);
	uint16_t vwv[14];

	size = MIN(size, max_write);

	SCVAL(vwv+0, 0, 0xFF);
	SCVAL(vwv+0, 1, 0);
	SSVAL(vwv+1, 0, 0);
	SSVAL(vwv+2, 0, fnum);
	SIVAL(vwv+3, 0, offset);
	SIVAL(vwv+5, 0, 0);
	SSVAL(vwv+7, 0, mode);
	SSVAL(vwv+8, 0, 0);
	SSVAL(vwv+9, 0, (size>>16));
	SSVAL(vwv+10, 0, size);

	SSVAL(vwv+11, 0,
	      cli_wct_ofs(cli)
	      + 1		/* the wct field */
	      + wct * 2		/* vwv */
	      + 2		/* num_bytes field */
	      + 1		/* pad */);

	if (bigoffset) {
		SIVAL(vwv+12, 0, (((uint64_t)offset)>>32) & 0xffffffff);
	}

	return cli_request_send(mem_ctx, ev, cli, SMBwriteX, 0, wct, vwv,
				2, size, buf);
}

NTSTATUS cli_write_andx_recv(struct async_req *req, size_t *pwritten)
{
	uint8_t wct;
	uint16_t *vwv;
	uint16_t num_bytes;
	uint8_t *bytes;
	NTSTATUS status;
	size_t written;

	if (async_req_is_nterror(req, &status)) {
		return status;
	}

	status = cli_pull_reply(req, &wct, &vwv, &num_bytes, &bytes);

	if (NT_STATUS_IS_ERR(status)) {
		return status;
	}

	if (wct < 6) {
		return NT_STATUS_INVALID_NETWORK_RESPONSE;
	}

	written = SVAL(vwv+2, 0);
	written |= SVAL(vwv+4, 0)<<16;
	*pwritten = written;

	return NT_STATUS_OK;
}

struct cli_writeall_state {
	struct event_context *ev;
	struct cli_state *cli;
	uint16_t fnum;
	uint16_t mode;
	const uint8_t *buf;
	off_t offset;
	size_t size;
	size_t written;
};

static void cli_writeall_written(struct async_req *req);

static struct async_req *cli_writeall_send(TALLOC_CTX *mem_ctx,
					   struct event_context *ev,
					   struct cli_state *cli,
					   uint16_t fnum,
					   uint16_t mode,
					   const uint8_t *buf,
					   off_t offset, size_t size)
{
	struct async_req *result;
	struct async_req *subreq;
	struct cli_writeall_state *state;

	if (!async_req_setup(mem_ctx, &result, &state,
			     struct cli_writeall_state)) {
		return NULL;
	}
	state->ev = ev;
	state->cli = cli;
	state->fnum = fnum;
	state->mode = mode;
	state->buf = buf;
	state->offset = offset;
	state->size = size;
	state->written = 0;

	subreq = cli_write_andx_send(state, state->ev, state->cli, state->fnum,
				     state->mode, state->buf, state->offset,
				     state->size);
	if (subreq == NULL) {
		goto fail;
	}

	subreq->async.fn = cli_writeall_written;
	subreq->async.priv = result;
	return result;

 fail:
	TALLOC_FREE(result);
	return NULL;
}

static void cli_writeall_written(struct async_req *subreq)
{
	struct async_req *req = talloc_get_type_abort(
		subreq->async.priv, struct async_req);
	struct cli_writeall_state *state = talloc_get_type_abort(
		req->private_data, struct cli_writeall_state);
	NTSTATUS status;
	size_t written, to_write;

	status = cli_write_andx_recv(subreq, &written);
	TALLOC_FREE(subreq);
	if (!NT_STATUS_IS_OK(status)) {
		async_req_nterror(req, status);
		return;
	}

	state->written += written;

	if (state->written > state->size) {
		async_req_nterror(req, NT_STATUS_INVALID_NETWORK_RESPONSE);
		return;
	}

	to_write = state->size - state->written;

	if (to_write == 0) {
		async_req_done(req);
		return;
	}

	subreq = cli_write_andx_send(state, state->ev, state->cli, state->fnum,
				     state->mode,
				     state->buf + state->written,
				     state->offset + state->written, to_write);
	if (subreq == NULL) {
		async_req_nterror(req, NT_STATUS_NO_MEMORY);
		return;
	}

	subreq->async.fn = cli_writeall_written;
	subreq->async.priv = req;
}

static NTSTATUS cli_writeall_recv(struct async_req *req)
{
	return async_req_simple_recv_ntstatus(req);
}

struct cli_push_write_state {
	struct async_req *req;/* This is the main request! Not the subreq */
	uint32_t idx;
	off_t ofs;
	uint8_t *buf;
	size_t size;
};

struct cli_push_state {
	struct event_context *ev;
	struct cli_state *cli;
	uint16_t fnum;
	uint16_t mode;
	off_t start_offset;
	size_t window_size;

	size_t (*source)(uint8_t *buf, size_t n, void *priv);
	void *priv;

	bool eof;

	size_t chunk_size;
	off_t next_offset;

	/*
	 * Outstanding requests
	 */
	uint32_t pending;
	uint32_t num_reqs;
	struct cli_push_write_state **reqs;
};

static void cli_push_written(struct async_req *req);

static bool cli_push_write_setup(struct async_req *req,
				 struct cli_push_state *state,
				 uint32_t idx)
{
	struct cli_push_write_state *substate;
	struct async_req *subreq;

	substate = talloc(state->reqs, struct cli_push_write_state);
	if (!substate) {
		return false;
	}
	substate->req = req;
	substate->idx = idx;
	substate->ofs = state->next_offset;
	substate->buf = talloc_array(substate, uint8_t, state->chunk_size);
	if (!substate->buf) {
		talloc_free(substate);
		return false;
	}
	substate->size = state->source(substate->buf,
				       state->chunk_size,
				       state->priv);
	if (substate->size == 0) {
		state->eof = true;
		/* nothing to send */
		talloc_free(substate);
		return true;
	}

	subreq = cli_writeall_send(substate,
				   state->ev, state->cli,
				   state->fnum, state->mode,
				   substate->buf,
				   substate->ofs,
				   substate->size);
	if (!subreq) {
		talloc_free(substate);
		return false;
	}
	subreq->async.fn = cli_push_written;
	subreq->async.priv = substate;

	state->reqs[idx] = substate;
	state->pending += 1;
	state->next_offset += substate->size;

	return true;
}

struct async_req *cli_push_send(TALLOC_CTX *mem_ctx, struct event_context *ev,
				struct cli_state *cli,
				uint16_t fnum, uint16_t mode,
				off_t start_offset, size_t window_size,
				size_t (*source)(uint8_t *buf, size_t n,
						 void *priv),
				void *priv)
{
	struct async_req *req;
	struct cli_push_state *state;
	uint32_t i;

	if (!async_req_setup(mem_ctx, &req, &state,
			     struct cli_push_state)) {
		return NULL;
	}
	state->cli = cli;
	state->ev = ev;
	state->fnum = fnum;
	state->start_offset = start_offset;
	state->mode = mode;
	state->source = source;
	state->priv = priv;
	state->eof = false;
	state->pending = 0;
	state->next_offset = start_offset;

	state->chunk_size = cli_write_max_bufsize(cli, mode);

	if (window_size == 0) {
		window_size = cli->max_mux * state->chunk_size;
	}
	state->num_reqs = window_size/state->chunk_size;
	if ((window_size % state->chunk_size) > 0) {
		state->num_reqs += 1;
	}
	state->num_reqs = MIN(state->num_reqs, cli->max_mux);
	state->num_reqs = MAX(state->num_reqs, 1);

	state->reqs = TALLOC_ZERO_ARRAY(state, struct cli_push_write_state *,
					state->num_reqs);
	if (state->reqs == NULL) {
		goto failed;
	}

	for (i=0; i<state->num_reqs; i++) {
		if (!cli_push_write_setup(req, state, i)) {
			goto failed;
		}

		if (state->eof) {
			break;
		}
	}

	if (state->pending == 0) {
		if (!async_post_ntstatus(req, ev, NT_STATUS_OK)) {
			goto failed;
		}
		return req;
	}

	return req;

 failed:
	TALLOC_FREE(req);
	return NULL;
}

static void cli_push_written(struct async_req *subreq)
{
	struct cli_push_write_state *substate = talloc_get_type_abort(
		subreq->async.priv, struct cli_push_write_state);
	struct async_req *req = substate->req;
	struct cli_push_state *state = talloc_get_type_abort(
		req->private_data, struct cli_push_state);
	NTSTATUS status;
	uint32_t idx = substate->idx;

	state->reqs[idx] = NULL;
	state->pending -= 1;

	status = cli_writeall_recv(subreq);
	TALLOC_FREE(subreq);
	TALLOC_FREE(substate);
	if (!NT_STATUS_IS_OK(status)) {
		async_req_nterror(req, status);
		return;
	}

	if (!state->eof) {
		if (!cli_push_write_setup(req, state, idx)) {
			async_req_nomem(NULL, req);
			return;
		}
	}

	if (state->pending == 0) {
		async_req_done(req);
		return;
	}
}

NTSTATUS cli_push_recv(struct async_req *req)
{
	return async_req_simple_recv_ntstatus(req);
}

NTSTATUS cli_push(struct cli_state *cli, uint16_t fnum, uint16_t mode,
		  off_t start_offset, size_t window_size,
		  size_t (*source)(uint8_t *buf, size_t n, void *priv),
		  void *priv)
{
	TALLOC_CTX *frame = talloc_stackframe();
	struct event_context *ev;
	struct async_req *req;
	NTSTATUS result = NT_STATUS_NO_MEMORY;

	if (cli->fd_event != NULL) {
		/*
		 * Can't use sync call while an async call is in flight
		 */
		return NT_STATUS_INVALID_PARAMETER;
	}

	ev = event_context_init(frame);
	if (ev == NULL) {
		goto nomem;
	}

	req = cli_push_send(frame, ev, cli, fnum, mode, start_offset,
			    window_size, source, priv);
	if (req == NULL) {
		goto nomem;
	}

	while (req->state < ASYNC_REQ_DONE) {
		event_loop_once(ev);
	}

	result = cli_push_recv(req);
 nomem:
	TALLOC_FREE(frame);
	return result;
}
