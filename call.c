#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sco.h>
#include <bluetooth/l2cap.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>

/*
 * bluetooth handsfree profile helpers
 */

#define HFP_HF_ECNR	(1 << 0)
#define HFP_HF_CW	(1 << 1)
#define HFP_HF_CID	(1 << 2)
#define HFP_HF_VOICE	(1 << 3)
#define HFP_HF_VOLUME	(1 << 4)
#define HFP_HF_STATUS	(1 << 5)
#define HFP_HF_CONTROL	(1 << 6)

#define HFP_AG_CW	(1 << 0)
#define HFP_AG_ECNR	(1 << 1)
#define HFP_AG_VOICE	(1 << 2)
#define HFP_AG_RING	(1 << 3)
#define HFP_AG_TAG	(1 << 4)
#define HFP_AG_REJECT	(1 << 5)
#define HFP_AG_STATUS	(1 << 6)
#define HFP_AG_CONTROL	(1 << 7)
#define HFP_AG_ERRORS	(1 << 8)

#define HFP_CIND_UNKNOWN	-1
#define HFP_CIND_NONE		0
#define HFP_CIND_SERVICE	1
#define HFP_CIND_CALL		2
#define HFP_CIND_CALLSETUP	3
#define HFP_CIND_CALLHELD	4
#define HFP_CIND_SIGNAL		5
#define HFP_CIND_ROAM		6
#define HFP_CIND_BATTCHG	7

/* call indicator values */
#define HFP_CIND_CALL_NONE	0
#define HFP_CIND_CALL_ACTIVE	1

/* callsetup indicator values */
#define HFP_CIND_CALLSETUP_NONE		0
#define HFP_CIND_CALLSETUP_INCOMING	1
#define HFP_CIND_CALLSETUP_OUTGOING	2
#define HFP_CIND_CALLSETUP_ALERTING	3

/* service indicator values */
#define HFP_CIND_SERVICE_NONE		0
#define HFP_CIND_SERVICE_AVAILABLE	1

/*!
 * \brief This struct holds HFP features that we support.
 */
struct hfp_hf {
	int ecnr:1;	/*!< echo-cancel/noise reduction */
	int cw:1;	/*!< call waiting and three way calling */
	int cid:1;	/*!< cli presentation (callier id) */
	int voice:1;	/*!< voice recognition activation */
	int volume:1;	/*!< remote volume control */
	int status:1;	/*!< enhanced call status */
	int control:1;	/*!< enhanced call control*/
};

/*!
 * \brief This struct holds HFP features the AG supports.
 */
struct hfp_ag {
	int cw:1;	/*!< three way calling */
	int ecnr:1;	/*!< echo-cancel/noise reduction */
	int voice:1;	/*!< voice recognition */
	int ring:1;	/*!< in band ring tone capability */
	int tag:1;	/*!< attach a number to a voice tag */
	int reject:1;	/*!< ability to reject a call */
	int status:1;	/*!< enhanced call status */
	int control:1;	/*!< enhanced call control*/
	int errors:1;	/*!< extended error result codes*/
};

/*!
 * \brief This struct holds mappings for indications.
 */
struct hfp_cind {
	int service;	/*!< whether we have service or not */
	int call;	/*!< call state */
	int callsetup;	/*!< bluetooth call setup indications */
	int callheld;	/*!< bluetooth call hold indications */
	int signal;	/*!< signal strength */
	int roam;	/*!< roaming indicator */
	int battchg;	/*!< battery charge indicator */
};


/*!
 * \brief This struct holds state information about the current hfp connection.
 */
struct hfp_pvt {
	struct mbl_pvt *owner;		/*!< the mbl_pvt struct that owns this struct */
	int initialized:1;		/*!< whether a service level connection exists or not */
	int nocallsetup:1;		/*!< whether we detected a callsetup indicator */
	struct hfp_ag brsf;		/*!< the supported feature set of the AG */
	int cind_index[16];		/*!< the cind/ciev index to name mapping for this AG */
	int cind_state[16];		/*!< the cind/ciev state for this AG */
	struct hfp_cind cind_map;	/*!< the cind name to index mapping for this AG */
	int rsock;			/*!< our rfcomm socket */
	int rport;			/*!< our rfcomm port */
	int sent_alerting;		/*!< have we sent alerting? */
};


/* Our supported features.
 * we only support caller id
 */
static struct hfp_hf hfp_our_brsf = {
	.ecnr = 0,
	.cw = 0,
	.cid = 1,
	.voice = 0,
	.volume = 0,
	.status = 0,
	.control = 0,
};
// static int rfcomm_connect(bdaddr_t src, bdaddr_t dst, int remote_channel);
// static int rfcomm_write(int rsock, char *buf);
static int rfcomm_write_full(int rsock, char *buf, size_t count);
static int rfcomm_wait(int rsock, int *ms);
static ssize_t rfcomm_read(int rsock, char *buf, size_t count);


/*!
 * \brief Send ATD.
 * \param hfp an hfp_pvt struct
 * \param number the number to send
 */
static int hfp_send_atd(struct hfp_pvt *hfp, const char *number)
{
	char cmd[64];
	snprintf(cmd, sizeof(cmd), "ATD%s;\r", number);
//	return rfcomm_write(hfp->rsock, cmd);
	return 1;
}

/*!
 * \brief Write to an rfcomm socket.
 * \param rsock the socket to write to
 * \param buf the buffer to write
 * \param count the number of characters from the buffer to write
 *
 * This function will write count characters from buf.  It will always write
 * count chars unless it encounters an error.
 *
 * \retval -1 error
 * \retval 0 success
 */
static int rfcomm_write_full(int rsock, char *buf, size_t count)
{
	char *p = buf;
	ssize_t out_count;

	printf("rfcomm_write() (%d) [%.*s]\n", rsock, (int) count, buf);
//	while (count > 0) {
//		if ((out_count = write(rsock, p, count)) == -1) {
//			printf("rfcomm_write() error [%d]\n", errno);
//			return -1;
//		}
//		count -= out_count;
//		p += out_count;
//	}

	return 0;
}

/*!
 * \brief Wait for activity on an rfcomm socket.
 * \param rsock the socket to watch
 * \param ms a pointer to an int containing a timeout in ms
 * \return zero on timeout and the socket fd (non-zero) otherwise
 * \retval 0 timeout
 */
static int rfcomm_wait(int rsock, int *ms)
{
//	int exception, outfd;
//	outfd = ast_waitfor_n_fd(&rsock, 1, ms, &exception);
//	if (outfd < 0)
//		outfd = 0;
//
//	return outfd;
	return 1;
}

static int sco_connect(bdaddr_t src, bdaddr_t dst)
{
	struct sockaddr_sco addr;
	int s;

	if ((s = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_SCO)) < 0) {
		// printf("socket() failed (%d).\n", errno);
		return -1;
	}

/* XXX this does not work with the do_sco_listen() thread (which also bind()s
 * to this address).  Also I am not sure if it is necessary. */
#if 0
	memset(&addr, 0, sizeof(addr));
	addr.sco_family = AF_BLUETOOTH;
	bacpy(&addr.sco_bdaddr, &src);
	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		printf("bind() failed (%d).\n", errno);
		close(s);
		return -1;
	}
#endif

	memset(&addr, 0, sizeof(addr));
	addr.sco_family = AF_BLUETOOTH;
	bacpy(&addr.sco_bdaddr, &dst);

	if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		// printf("sco connect() failed (%d).\n", errno);
		// close(s);
		return -1;
	}

	return s;
}



int rfcomm_connect(char dest[18])
{
    struct sockaddr_rc addr = { 0 };
    int s, status;
    // char dest[18];
    // dest = d; //"01:23:45:67:89:AB";

    // allocate a socket
    s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

    // set the connection parameters (who to connect to)
    addr.rc_family = AF_BLUETOOTH;
    addr.rc_channel = (uint8_t) 1;
    str2ba( dest, &addr.rc_bdaddr );

    // connect to server
    status = connect(s, (struct sockaddr *)&addr, sizeof(addr));

    // send a message
    if (status == 0) {
        status = write(s, "hello!", 6);
    }

    if (status < 0) perror("uh oh");

    close(s);
    return 0;
}


///* Request authentication */
//
//static void cmd_auth(int dev_id, int argc, char **argv)
//{
//        struct hci_conn_info_req *cr;
//        bdaddr_t bdaddr;
//        int opt, dd;
//
//        for_each_opt(opt, auth_options, NULL) {
//            switch (opt) {
//            default:
//                printf("%s", auth_help);
//                return;
//            }
//        }
//        helper_arg(1, 1, &argc, &argv, auth_help);
//
//        str2ba(argv[0], &bdaddr);
//
//        if (dev_id < 0) {
//            dev_id = hci_for_each_dev(HCI_UP, find_conn, (long) &bdaddr);
//            if (dev_id < 0) {
//                fprintf(stderr, "Not connected.\n");
//                exit(1);
//            }
//        }
//
//        dd = hci_open_dev(dev_id);
//        if (dd < 0) {
//            perror("HCI device open failed");
//            exit(1);
//        }
//
//        cr = malloc(sizeof(*cr) + sizeof(struct hci_conn_info));
//        if (!cr) {
//            perror("Can't allocate memory");
//            exit(1);
//        }
//
//        bacpy(&cr->bdaddr, &bdaddr);
//        cr->type = ACL_LINK;
//        if (ioctl(dd, HCIGETCONNINFO, (unsigned long) cr) < 0) {
//            perror("Get connection info failed");
//            exit(1);
//        }
//
//        if (hci_authenticate_link(dd, htobs(cr->conn_info->handle), 25000) < 0) {
//            perror("HCI authentication request failed");
//            exit(1);
//        }
//
//        free(cr);
//
//        hci_close_dev(dd);
//}

int main() {

  printf("starting ...\n");

  // Get bluetooth device id
  int device_id = hci_get_route(NULL); // Passing NULL argument will retrieve the id of first avalaibe device
  if (device_id < 0) {
    printf("Error: Bluetooth device not found");
    return 1;
  }

  // Find nearby devices
  int len     = 8;                 // Search time = 1.28 * len seconds
  int max_rsp = 255;               // Return maximum max_rsp devices
  int flags   = IREQ_CACHE_FLUSH;  // Flush out the cache of previously detected devices.
  inquiry_info* i_infs = (inquiry_info*) malloc(max_rsp * sizeof(inquiry_info));
  int num_rsp = hci_inquiry(device_id, len, max_rsp, NULL, &i_infs, flags); // search
  if (num_rsp < 0) {
    printf("Error: the hci_inquiry fails");
    exit(1);
  }
  printf("Found %d device\n", num_rsp);

  // Open socket
  int socket = hci_open_dev(device_id);
  if (socket < 0) {
    printf("Error: Cannot open socket");
    return 1;
  }

  int i;
  for (i = 0; i < num_rsp; i++) {
    char device_address[20], device_name[300];
    inquiry_info* current_device = i_infs + i;
    // Get HEX address
    ba2str(&(current_device->bdaddr), device_address);
    // Clean previous name
    memset(device_name, 0, sizeof(device_name));
    // Get device name
    if (hci_read_remote_name(socket, &(current_device->bdaddr), sizeof(device_name), device_name, 0) < 0) {
        strcpy(device_name, "[unknown]"); // If cannot retrieve the name then set it "unknown"
    }
    // print address and name
    printf("%s  %s\n", device_address, device_name);

        uint16_t     handle;
	    unsigned int ptype      = HCI_DM1 | HCI_DM3 | HCI_DM5 | HCI_DH1 | HCI_DH3 | HCI_DH5;
    // Establish HCI connection with device
    if (hci_create_connection(socket, &(current_device->bdaddr), htobs(ptype), 0, 0, &handle, 0) < 0) {
        printf("HCI create connection error\n");
        close(socket);
    } else {
        printf("Connection: OK\n");

    }

  }

  // Close the socket
  close(socket);

  // Remove allocated memory
  free(i_infs);

  return 0;
}
