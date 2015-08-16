/*
 * 	w1d.c
 *
 * Copyright (c) 2004 Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 * 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <asm/byteorder.h>
#include <asm/types.h>

#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "w1_netlink.h"

static int need_exit;
static int send_seq;

static int send_cmd(FILE *out, int s, struct w1_netlink_msg *msg)
{
	struct cn_msg *cmsg;
	struct w1_netlink_msg *m;
	struct nlmsghdr *nlh;
	int size, err;
	
	size = NLMSG_SPACE(sizeof(struct cn_msg) + sizeof(struct w1_netlink_msg) + msg->len);

	nlh = malloc(size);
	if (!nlh)
		return -ENOMEM;
	
	memset(nlh, 0, size);
	
	nlh->nlmsg_seq = send_seq++;
	nlh->nlmsg_pid = getpid();
	nlh->nlmsg_type = NLMSG_DONE;
	nlh->nlmsg_len = NLMSG_LENGTH(size - sizeof(struct nlmsghdr));
	nlh->nlmsg_flags = 0;

	cmsg = NLMSG_DATA(nlh);
	
	cmsg->id.idx = CN_W1_IDX;
	cmsg->id.val = CN_W1_VAL;
	cmsg->seq = nlh->nlmsg_seq;
	cmsg->ack = 0;
	cmsg->len = sizeof(struct w1_netlink_msg) + msg->len;

	m = (struct w1_netlink_msg *)(cmsg + 1);
	memcpy(m, msg, sizeof(struct w1_netlink_msg));
	memcpy(m+1, msg->data, msg->len);
	
	err = send(s, nlh, size, 0);
	if (err == -1) {
		fprintf(out, "Failed to send: %s [%d].\n", strerror(errno), errno);
		free(nlh);
		return err;
	}
	free(nlh);

	return err;
}

static int w1_test_create_cmd(struct w1_netlink_msg *m, __u16 maxlen)
{
	char *cmd_data;
	__u16 i, newlen;
	struct w1_netlink_cmd *cmd;

	cmd_data = (char *)m->data;
	
	for (i=0; i<3; ++i) {
		newlen = (i+1)*10;

		if (newlen + sizeof(struct w1_netlink_cmd) + m->len > maxlen)
			break;

		cmd = (struct w1_netlink_cmd *)cmd_data;

		cmd->cmd = i;
		cmd->len = newlen;

		cmd_data += cmd->len + sizeof(struct w1_netlink_cmd);
		m->len += cmd->len + sizeof(struct w1_netlink_cmd);
	}

	return m->len;
}

static int w1_test_cmd_master(FILE *out, int s, __u32 id)
{
	char buf[1024];
	struct w1_netlink_msg *m;
	
	memset(buf, 0, sizeof(buf));

	m = (struct w1_netlink_msg *)buf;
	
	m->type = W1_MASTER_CMD;
	m->len = 0;
	m->id.mst.id = id;

	w1_test_create_cmd(m, sizeof(buf) - sizeof(struct w1_netlink_msg));

	return send_cmd(out, s, m);
}

static int w1_test_cmd_slave(FILE *out, int s, struct w1_reg_num *id)
{
	char buf[1024];
	struct w1_netlink_msg *m;
	struct w1_netlink_cmd *cmd;
	char *cmd_data;
	int i;
	
	memset(buf, 0, sizeof(buf));

	m = (struct w1_netlink_msg *)buf;
	
	m->type = W1_SLAVE_CMD;
	m->len = 0;
	memcpy(m->id.id, id, sizeof(m->id.id));

	cmd_data = (char *)m->data;
	
	for (i=0; i<3; ++i) {
		cmd = (struct w1_netlink_cmd *)cmd_data;

		cmd->cmd = i;
		cmd->len = (i+1)*10;
		
		cmd_data += cmd->len + sizeof(struct w1_netlink_cmd);

		m->len += cmd->len + sizeof(struct w1_netlink_cmd);
	}

	return send_cmd(out, s, m);
}

static void w1_dump_reply(FILE *out, char *prefix, struct w1_netlink_msg *hdr)
{
	struct w1_netlink_cmd *cmd;
	unsigned char *hdr_data = hdr->data;
	unsigned int hdr_len = hdr->len;
	time_t tm;
	int i;

	time(&tm);
	
	while (hdr_len) {
		cmd = (struct w1_netlink_cmd *)hdr_data;
	
		fprintf(out, "%.24s : %s ", ctime(&tm), prefix);

		if (cmd->len + sizeof(struct w1_netlink_cmd) > hdr_len) {
			fprintf(out, "Malformed message.\n");
			break;
		}

		switch (cmd->cmd) {
			case W1_CMD_READ:
				fprintf(out, "READ: ");
				for (i=0; i<cmd->len; ++i)
					fprintf(out, "%02x ", cmd->data[i]);
				fprintf(out, "\n");
				break;
			default:
				fprintf(out, "cmd=%02x, len=%u.\n", cmd->cmd, cmd->len);
				break;
		}

		hdr_data += cmd->len + sizeof(struct w1_netlink_cmd);
		hdr_len -= cmd->len + sizeof(struct w1_netlink_cmd);
	}
}

static void w1_parse_master_reply(FILE *out, struct w1_netlink_msg *hdr)
{
	char prefix[128];

	snprintf(prefix, sizeof(prefix), "master: id=%08x", hdr->id.mst.id);
	w1_dump_reply(out, prefix, hdr);
}

static void w1_parse_slave_reply(FILE *out, struct w1_netlink_msg *hdr)
{
	char prefix[128];
	struct w1_reg_num id;
				
	memcpy(&id, hdr->id.id, sizeof(id));

	snprintf(prefix, sizeof(prefix), "slave: id=%02x.%012llx.%02x",
		id.family, (unsigned long long)id.id, id.crc); 
	w1_dump_reply(out, prefix, hdr);
}

void w1_dump_message(FILE *out, int s, struct cn_msg *msg)
{
	time_t tm;
	struct w1_netlink_msg *data = (struct w1_netlink_msg *)(msg + 1);
	unsigned int i;
	
	while (msg->len) {
		struct w1_reg_num id;
		
		time(&tm);
#if 0
		fprintf(out, "%.24s : %08x.%08x, len=%u, seq=%u, ack=%u, data->len=%u.\n", 
				ctime(&tm), msg->id.idx, msg->id.val, msg->len, msg->seq, msg->ack, data->len);
#endif
		switch (data->type) {
			case W1_MASTER_ADD:
			case W1_MASTER_REMOVE:
				if (data->type == W1_MASTER_ADD)
					w1_test_cmd_master(out, s, data->id.mst.id);
				
				fprintf(out, "%.24s : master has been %.8s: id=%08x.\n", 
					ctime(&tm), 
					(data->type == W1_MASTER_ADD)?"added":"removed",
					data->id.mst.id);
				break;
			
			case W1_SLAVE_ADD:
			case W1_SLAVE_REMOVE:
				memcpy(&id, data->id.id, sizeof(id));

				if (data->type == W1_SLAVE_ADD)
					w1_test_cmd_slave(out, s, &id);
				
				fprintf(out, "%.24s :  slave has been %.8s: id=%02x.%012llx.%02x\n", 
					ctime(&tm), 
					(data->type == W1_SLAVE_ADD)?"added":"removed",
					id.family, (unsigned long long)id.id, id.crc); 
				break;
			case W1_MASTER_CMD:
				w1_parse_master_reply(out, data);
				break;
			case W1_SLAVE_CMD:
				w1_parse_slave_reply(out, data);
				break;
			default:
				fprintf(out, "%.24s : type=%02x", ctime(&tm), data->type);
				
				for (i=0; i<sizeof(data->id.id); ++i)
					fprintf(out, "%02x.", data->id.id[i]);
				fprintf(out, "\n");
		}
		fflush(out);

		msg->len -= sizeof(struct w1_netlink_msg) + data->len;
		data = (struct w1_netlink_msg *)(((char *)data) + sizeof(struct w1_netlink_msg) + data->len);
	}
}

int main(int argc, char *argv[])
{
	int s;
	unsigned char buf[1024];
	int len;
	struct sockaddr_nl l_local;
	struct cn_msg *msg;
	FILE *out;
	struct pollfd pfd;
	struct nlmsghdr *reply;
	
	if (argc < 2)
		out = stdout;
	else {
		out = fopen(argv[1], "a+");
		if (!out) {
			fprintf(stderr, "Unable to open %s for writing: %s\n", 
					argv[1], strerror(errno));
			out = stdout;
		}
	}
	
	memset(buf, 0, sizeof(buf));
	
	s = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
	if (s == -1) {
		perror("socket");
		return -1;
	}
	
	l_local.nl_family = AF_NETLINK;
	l_local.nl_groups = 23;
	l_local.nl_pid    = getpid();
	
	if (bind(s, (struct sockaddr *)&l_local, sizeof(struct sockaddr_nl)) == -1) {
		perror("bind");
		close(s);
		return -1;
	}

	pfd.fd = s;

	while (!need_exit)
	{
		pfd.events = POLLIN;
		pfd.revents = 0;
		switch (poll(&pfd, 1, -1)) {
			case 0:
				need_exit = 1;
				break;
			case -1:
				if (errno != EINTR) {
					need_exit = 1;
					break;
				}
				continue;
			default:
				break;
		}
		if (need_exit)
			break;

		len = recv(s, buf, sizeof(buf), 0);
		if (len == -1) {
			perror("recv buf");
			close(s);
			return -1;
		}

		reply = (struct nlmsghdr *)buf;

		switch (reply->nlmsg_type) {
			case NLMSG_ERROR:
				fprintf(out, "Error message received.\n");
				fflush(out);
				break;
			case NLMSG_DONE:
				msg = (struct cn_msg *)NLMSG_DATA(reply);
				w1_dump_message(out, s, msg);
				break;
			default:
				break;
		}

	}
	
	close(s);
	return 0;
}

