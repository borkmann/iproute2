#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/if_link.h>

#include "rt_names.h"
#include "utils.h"
#include "ip_common.h"

static void explain(struct link_util *lu, FILE *f)
{
	fprintf(f,
		"Usage: ... %s [ mode MODE ] [ POLICY ] [ peer [ POLICY <options> ] ]\n"
		"\n"
		"MODE: l3 | l2\n"
		"POLICY: forward | blackhole\n"
		"(first values are the defaults if nothing is specified)\n"
		"\n"
		"To get <options> type 'ip link add help'.\n",
		lu->id);
}

bool seen_mode, seen_peer;
struct rtattr *data;

static int netkit_parse_opt(struct link_util *lu, int argc, char **argv,
			    struct nlmsghdr *n)
{
	__u32 ifi_flags, ifi_change, ifi_index;
	struct ifinfomsg *ifm, *peer_ifm;
	int err;

	ifm = NLMSG_DATA(n);
	ifi_flags = ifm->ifi_flags;
	ifi_change = ifm->ifi_change;
	ifi_index = ifm->ifi_index;
	ifm->ifi_flags = 0;
	ifm->ifi_change = 0;
	ifm->ifi_index = 0;
	while (argc > 0) {
		if (matches(*argv, "mode") == 0) {
			__u32 mode = 0;

			NEXT_ARG();
			if (seen_mode)
				duparg("mode", *argv);
			seen_mode = true;

			if (strcmp(*argv, "l3") == 0)
				mode = NETKIT_L3;
			else if (strcmp(*argv, "l2") == 0)
				mode = NETKIT_L2;
			else {
				fprintf(stderr, "Error: argument of \"mode\" must be either \"l3\" or \"l2\"\n");
				return -1;
			}
			addattr32(n, 1024, IFLA_NETKIT_MODE, mode);
		} else if (matches(*argv, "forward") == 0 ||
			   matches(*argv, "blackhole") == 0) {
			int attr_name = seen_peer ?
					IFLA_NETKIT_PEER_POLICY :
					IFLA_NETKIT_POLICY;
			__u32 policy = 0;

			if (strcmp(*argv, "forward") == 0)
				policy = NETKIT_PASS;
			else if (strcmp(*argv, "blackhole") == 0)
				policy = NETKIT_DROP;
			else {
				fprintf(stderr, "Error: argument of \"mode\" must be either \"forward\" or \"blackhole\"\n");
				return -1;
			}
			addattr32(n, 1024, attr_name, policy);
		} else if (matches(*argv, "peer") == 0) {
			if (seen_peer)
				duparg("peer", *(argv + 1));
			seen_peer = true;
		} else {
			char *type = NULL;

			if (seen_peer) {
				data = addattr_nest(n, 1024, IFLA_NETKIT_PEER_INFO);
				n->nlmsg_len += sizeof(struct ifinfomsg);
				err = iplink_parse(argc, argv, (struct iplink_req *)n, &type);
				if (err < 0)
					return err;
				if (type)
					duparg("type", argv[err]);
				goto out_ok;
			}
			fprintf(stderr, "%s: unknown option \"%s\"?\n",
				lu->id, *argv);
			explain(lu, stderr);
			return -1;
		}
		argc--;
		argv++;
	}
out_ok:
	if (data) {
		peer_ifm = RTA_DATA(data);
		peer_ifm->ifi_index = ifm->ifi_index;
		peer_ifm->ifi_flags = ifm->ifi_flags;
		peer_ifm->ifi_change = ifm->ifi_change;
		addattr_nest_end(n, data);
	}
	ifm->ifi_flags = ifi_flags;
	ifm->ifi_change = ifi_change;
	ifm->ifi_index = ifi_index;
	return 0;
}

static void netkit_print_opt(struct link_util *lu, FILE *f, struct rtattr *tb[])
{
	if (!tb)
		return;
	if (tb[IFLA_NETKIT_MODE] &&
	    RTA_PAYLOAD(tb[IFLA_NETKIT_MODE]) == sizeof(__u32)) {
		__u32 mode = rta_getattr_u32(tb[IFLA_NETKIT_MODE]);
		const char *mode_str =
			mode == NETKIT_L2 ? "l2" :
			mode == NETKIT_L3 ? "l3" : "unknown";

		print_string(PRINT_ANY, "mode", " mode %s ", mode_str);
	}
	if (tb[IFLA_NETKIT_PRIMARY] &&
	    RTA_PAYLOAD(tb[IFLA_NETKIT_PRIMARY]) == sizeof(__u8)) {
		__u8 primary = rta_getattr_u8(tb[IFLA_NETKIT_PRIMARY]);
		const char *type_str = primary ? "primary" : "peer";

		print_string(PRINT_ANY, "type", " type %s ", type_str);
	}
	if (tb[IFLA_NETKIT_POLICY] &&
	    RTA_PAYLOAD(tb[IFLA_NETKIT_POLICY]) == sizeof(__u32)) {
		__u32 policy = rta_getattr_u32(tb[IFLA_NETKIT_POLICY]);
		const char *policy_str =
			policy == NETKIT_PASS ? "forward" :
			policy == NETKIT_DROP ? "blackhole" : "unknown";

		print_string(PRINT_ANY, "policy", " policy %s ", policy_str);
	}
}

static void netkit_print_help(struct link_util *lu,
			      int argc, char **argv, FILE *f)
{
	explain(lu, f);
}

struct link_util netkit_link_util = {
	.id		= "netkit",
	.maxattr	= IFLA_NETKIT_MAX,
	.parse_opt	= netkit_parse_opt,
	.print_opt	= netkit_print_opt,
	.print_help	= netkit_print_help,
};
