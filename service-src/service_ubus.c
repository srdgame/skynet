#include "skynet.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <unistd.h>

#include <libubus.h>
#include <libubox/blobmsg_json.h>

#define HASH_SIZE 4096
#define DEFAULT_QUEUE_SIZE 1024

#define DEBUG_PRINT 1

struct skynet_ubus_msg {
	uint32_t source;
	uint32_t session;
	struct blob_buf buf;
	struct skynet_ubus* ctx;
};

struct ubus_msg_node {
	struct ubus_msg_node * next;
	struct skynet_ubus_msg * msg;
};

struct ubus_msg_list {
	struct ubus_msg_node * head;
	struct ubus_msg_node * tail;
};

struct skynet_ubus {
	int timeout;
	uint32_t slave; // The service which launch this instance
	char socket_path[512];
	struct ubus_auto_conn *conn;
	struct ubus_msg_list *list;

	struct skynet_context *ctx;
};

static void
ubus_msg_list_push(struct ubus_msg_list * list, struct skynet_ubus_msg * m) {
#ifdef DEBUG_PRINT
	fprintf(stderr, "+\n");
#endif
	struct ubus_msg_node * node = skynet_malloc(sizeof(struct ubus_msg_node));
	node->next = NULL;
	node->msg = m;
	if (list->tail) {
		list->tail->next = node;
		list->tail = node;
	} else {
		list->head = list->tail = node;
	}
}

static void
ubus_msg_list_pop(struct ubus_msg_list * list, struct skynet_ubus_msg * m) {
#ifdef DEBUG_PRINT
	fprintf(stderr, "-\n");
#endif
	if (list->head == NULL || list->tail == NULL) {
		fprintf(stderr, "%s\n", "Wow bugs found!");
		return;
	}
	struct ubus_msg_node * node = list->head;
	struct ubus_msg_node * prev = NULL;
	while (node) {
		if (node->msg == m) {
			if (node == list->tail) {
				list->tail = prev;
			}
			if (prev == NULL) {
				list->head = node->next;
			} else {
				prev->next = node->next;
			}
			break;
		}
		prev = node;
		node = node->next;
	}
	skynet_free(node);
}

static struct ubus_msg_list *
ubus_msg_list_new() {
	struct ubus_msg_list * list = skynet_malloc(sizeof(struct ubus_msg_list));
	list->head = NULL;
	list->tail = NULL;
	return list;
}

static void
ubus_msg_list_free(struct ubus_msg_list *list) {
	while (list->head != NULL) {
		struct ubus_msg_node * node = list->head;
		list->head = node->next;
		skynet_free(node);
	}
	skynet_free(list);
}

static void
skynet_ubus_connect_cb(struct ubus_context *ctx) {
	fprintf(stderr, "Connection is ready");
	// skynet_error(h->ctx, "Connection is ready");
}

struct skynet_ubus *
ubus_create(void) {
	struct skynet_ubus * h = skynet_malloc(sizeof(*h));
	memset(h, 0, sizeof(struct skynet_ubus));
	h->timeout = 30;
	h->list = ubus_msg_list_new();
	h->conn = skynet_malloc(sizeof(struct ubus_auto_conn));
	h->conn->cb = skynet_ubus_connect_cb;
	return h;
}

void
ubus_release(struct skynet_ubus *h) {
	ubus_auto_shutdown(h->conn);
	skynet_free(h->conn);
	ubus_msg_list_free(h->list);
	skynet_free(h);
}

static int
send_to_caller(struct skynet_context * context, uint32_t destination , int session, int result, void * data, size_t sz) {
	void* buf = skynet_malloc(sz + 7);
	memset(buf, 0, sz + 7);
	if (result != 0) {
		sprintf(buf, "%s", "TRUE");
	} else {
		skynet_error(context, "%s", data);
		sprintf(buf, "%s", "FALSE");
	}
	memcpy(buf + 6, data, sz);

#ifdef DEBUG_PRINT
	printf("Send to caller %08x:%d \t%s\n", destination, session, (char*)data);
#endif
	return skynet_send(context, 0, destination, PTYPE_TEXT | PTYPE_TAG_DONTCOPY, session, buf, sz + 6);
}

static struct skynet_ubus_msg*
create_ubus_msg(struct skynet_ubus * h, int session, uint32_t source) {
	struct skynet_ubus_msg * msg = skynet_malloc(sizeof(struct skynet_ubus_msg));
	memset(&msg->buf, 0, sizeof(msg->buf));
	msg->ctx = h;
	msg->session = session;
	msg->source = source;

	return msg;
}

static void
release_ubus_msg(struct skynet_ubus_msg * msg) {
	skynet_free(msg);
}


static void
skynet_ubus_call_cb(struct ubus_request *req, int type, struct blob_attr *msg) {
	char * str;
	struct skynet_ubus_msg * umsg = (struct skynet_ubus_msg *)req->priv;
	struct skynet_ubus * h = umsg->ctx;

	ubus_msg_list_pop(h->list, umsg);

	if (!msg) {
		char buf[128];
		int sz = sprintf(buf, "ubus returns null, type: %d", type);
		send_to_caller(h->ctx, umsg->source, umsg->session, 0, buf, sz);
		release_ubus_msg(umsg);
		return;
	}

	str = blobmsg_format_json(msg, true);
#ifdef DEBUG_PRINT
	fprintf(stderr, "%s\n", str);
#endif
	send_to_caller(h->ctx, umsg->source, umsg->session, 1, str, strlen(str));
	free(str);
	release_ubus_msg(umsg);
}

static void
skynet_ubus_call(struct skynet_ubus * h, int session, uint32_t source, const char * path, const char * method, const char * json_str) {
	struct skynet_ubus_msg * msg = create_ubus_msg(h, session, source);
	blob_buf_init(&msg->buf, 0);

	skynet_error(h->ctx, "ubus_call from: %08x  path = %s method = %s json = %s", source, path, method, json_str);

	if (strlen(json_str) > 0 && !blobmsg_add_json_from_string(&msg->buf, json_str)) {
		char* err = "Cannot create blobmsg from json string";
#ifdef DEBUG_PRINT
		fprintf(stderr, err);
#endif
		send_to_caller(h->ctx, source, session, 0, err, strlen(err));
		release_ubus_msg(msg);
		return;
	}

	uint32_t id = 0;
	int ret = ubus_lookup_id(&h->conn->ctx, path, &id);
	if (ret != UBUS_STATUS_OK) {
		char* err = "Cannot find path id";
#ifdef DEBUG_PRINT
		fprintf(stderr, err);
#endif
		send_to_caller(h->ctx, source, session, 0, err, strlen(err));
		release_ubus_msg(msg);
		return;
	}

	ubus_msg_list_push(h->list, msg);

	int rv = ubus_invoke(&h->conn->ctx, id, method, msg->buf.head, skynet_ubus_call_cb, msg, h->timeout * 1000);
	if (rv != UBUS_STATUS_OK) {
		char tmp[256];
		int sz = sprintf(tmp, "ubus_invoke failed, err = %s", ubus_strerror(rv));
#ifdef DEBUG_PRINT
		fprintf(stderr, tmp);
#endif
		send_to_caller(h->ctx, source, session, 0, tmp, sz);

		ubus_msg_list_pop(h->list, msg);
		release_ubus_msg(msg);
	}
}

static void
skynet_ubus_objects_cb(struct ubus_context *c, struct ubus_object_data *obj, void *priv)
{
	char * str;
	struct skynet_ubus_msg * msg = (struct skynet_ubus_msg *) priv;
	struct skynet_ubus * h = msg->ctx;


#ifdef DEBUG_PRINT
	printf("%s\n", "skynet_ubus_objects_cb");
#endif

	ubus_msg_list_pop(h->list, msg);

	if (!obj->signature)
	{
		char* err = "Object not signatured!";
		send_to_caller(h->ctx, msg->source, msg->session, 0, err, strlen(err));
		release_ubus_msg(msg);
		return;
	}

	str = blobmsg_format_json(obj->signature, true);
#ifdef DEBUG_PRINT
	printf("%s\n", str);
#endif
	send_to_caller(h->ctx, msg->source, msg->session, 1, str, strlen(str));
	free(str);
	release_ubus_msg(msg);
}

static void
skynet_ubus_objects(struct skynet_ubus * h, int session, uint32_t source, const char* path) {
	struct skynet_ubus_msg * msg = create_ubus_msg(h, session, source);
	if (strlen(path) == 0) {
		path = NULL;
	}
	ubus_msg_list_push(h->list, msg);
	int rv = ubus_lookup(&h->conn->ctx, path, skynet_ubus_objects_cb, msg);
	if (rv != UBUS_STATUS_OK) {
		char tmp[256];
		int sz = sprintf(tmp, "ubus_lookup failed, err = %s", ubus_strerror(rv));
#ifdef DEBUG_PRINT
		fprintf(stderr, tmp);
#endif
		send_to_caller(h->ctx, msg->source, msg->session, 0, tmp, sz);

		ubus_msg_list_pop(h->list, msg);
		release_ubus_msg(msg);
	}
}

static const char*
_ubus_service_parm(char *msg, int sz, int* command_sz) {
	int i = *command_sz;
	char* ret = msg + i;
	while (i < sz) {
		if (msg[i] == ' ') {
			msg[i] = '\0';
			++i;
			break;
		}
		++i;
	}
	*command_sz = i;
	return ret;
}

static void
ubus_service_command(struct skynet_context * context, struct skynet_ubus * h, const char * msg, size_t sz, int session, uint32_t source) {
	int i = 0;
	char tmp[sz+1];
	memcpy(tmp, msg, sz);
	tmp[sz] = '\0';
#ifdef DEBUG_PRINT
	printf("Command %s from %08x:%d\n", msg, source, session);
#endif

	const char* cmd = _ubus_service_parm(tmp, sz, &i);
	if (memcmp(cmd, "call", i) == 0) {
		const char* path = _ubus_service_parm(tmp, sz, &i);
		const char* method = _ubus_service_parm(tmp, sz, &i);
		const char* json_str = _ubus_service_parm(tmp, sz, &i);
		skynet_ubus_call(h, session, source, path, method, json_str);
		return;
	}
	else if (memcmp(cmd, "list", i) == 0) {
		const char* path = _ubus_service_parm(tmp, sz, &i);
		skynet_ubus_objects(h, session, source, path);
		return;
	}
#ifdef DEBUG_PRINT
	printf("%s\n", "Unknown command!");
#endif
	skynet_error(context, "Unknown command %s", cmd);
	return;
}

static int
ubus_service_mainloop(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct skynet_ubus * h = ud;
	switch (type) {
		case PTYPE_TEXT:
			skynet_error(context, "recv bus request from %x, msg = %s", source, (char*)msg);
			ubus_service_command(context, h, msg, sz, session, source);
			break;
		default:
			skynet_error(context, "recv invalid message from %x, type = %d, msg = %s", source, type, (char*)msg);
			if (session != 0 && type != PTYPE_ERROR) {
				skynet_send(context,0,source,PTYPE_ERROR, session, NULL, 0);
			}
			break;
	}
	return 0;
}

int
ubus_init(struct skynet_ubus *h, struct skynet_context *ctx, const char * args) {
	h->ctx = ctx;
	sscanf(args,"%u %s", &h->slave, h->socket_path);
	if (h->slave == 0) {
		return 1;
	}
	skynet_error(ctx, "connect to ubus %s\tslave = %08x", h->socket_path, h->slave);
	if (strlen(h->socket_path) > 0)
		h->conn->path = h->socket_path;
	skynet_callback(ctx, h, ubus_service_mainloop);
	ubus_auto_connect(h->conn);
	return 0;
}
