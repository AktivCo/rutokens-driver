#include <stdlib.h>
#include <string.h>
#include "convert_apdu.h"
#include "debug.h"


int read_tag(unsigned char *buf, size_t buf_len,
		unsigned char tag_in, unsigned char *out, size_t out_len);

void swap_pair(unsigned char *buf, size_t len)
{
	size_t i;
	unsigned char tmp;

	for (i = 0; i + 1 < len; i += 2) {
		tmp = buf[i];
		buf[i] = buf[i + 1];
		buf[i + 1] = tmp;
	}
}

void swap_four(unsigned char *buf, size_t len)
{
	size_t i;
	unsigned char tmp;

	for (i = 0; i + 3 < len; i += 4) {
		tmp = buf[i];
		buf[i] = buf[i + 3];
		buf[i + 3] = tmp;

		swap_pair(&buf[i + 1], 2);
	}
}

int read_tag(unsigned char *buf, size_t buf_len,
		unsigned char tag_in, unsigned char *out, size_t out_len)
{
	unsigned char tag;
	size_t taglen, i = 0;

	while (i + 2 <= buf_len) {
		tag = buf[i];
		taglen = buf[i + 1];
		i += 2;
		if (taglen + i > buf_len)
			return -1;
		if (tag == tag_in) {
			if (taglen != out_len)
				return -1;
			memcpy(out, buf + i, out_len);
			return 0;
		}
		i += taglen;
	}
	return -1;
}

int convert_doinfo_to_rtprot(void *data, size_t data_len)
{
	unsigned char dohdr[32] = { 0 };
	unsigned char secattr[40], data_a5[0xff];
	unsigned char *p = data;
	size_t i, data_a5_len;

	if (read_tag(p, data_len, 0x80, &dohdr[0], 2) == 0) {
		swap_pair(&dohdr[0], 2);
		DEBUG_INFO3("tag 0x80 (file size) = %02x %02x", dohdr[0], dohdr[1]);
	}
	data_a5_len = dohdr[1] & 0xff;
	if (read_tag(p, data_len, 0xA5, data_a5, data_a5_len) == 0)
		DEBUG_INFO2("tag 0xA5 = %s", ct_hexdump(data_a5, data_a5_len));
	else
		data_a5_len = 0;
	if (data_len < sizeof(dohdr) + data_a5_len) {
		DEBUG_INFO2("data_len = %u", data_len);
		return -1;
	}
	if (read_tag(p, data_len, 0x83, &dohdr[2], 2) == 0)
		DEBUG_INFO3("tag 0x83 (Type,ID) = %02x %02x", dohdr[2], dohdr[3]);
	if (read_tag(p, data_len, 0x85, &dohdr[4], 3) == 0)
		/* ifd_debug(6, "tag 0x85 (Opt,Flags,MaxTry) = %02x %02x %02x",
				dohdr[4], dohdr[5], dohdr[6]) */;
	if (read_tag(p, data_len, 0x86, secattr, sizeof(secattr)) == 0) {
		i = 17;
		memcpy(dohdr + i, secattr, 8);
		for (i += 8, p = &secattr[8]; i < sizeof(dohdr); ++i, p += 4)
			dohdr[i] = *p;
		DEBUG_INFO2("tag 0x86 = %s", ct_hexdump(&dohdr[17], 15));
	}
	memcpy(data, dohdr, sizeof(dohdr));
	memcpy((unsigned char*)data + sizeof(dohdr), data_a5, data_a5_len);
	return sizeof(dohdr) + data_a5_len;
}

int convert_fcp_to_rtprot(void *data, size_t data_len)
{
	unsigned char rtprot[32] = { 0 };
	unsigned char secattr[40];
	unsigned char *p = data;
	size_t i;

	if (data_len < sizeof(rtprot)) {
		DEBUG_INFO2("data_len = %u", data_len);
		return -1;
	}
	/* 0x62 - FCP */
	if (p[0] != 0x62  ||  (size_t)p[1] + 2 > data_len) {
		DEBUG_INFO3("Tag = %02x  len = %u", p[0], p[1]);
		return -1;
	}
	p += 2;
	data_len -= 2;
	/* file type */
	if (read_tag(p, data_len, 0x82, &rtprot[4], 2) != 0)
		return -1;
	DEBUG_INFO3("tag 0x82 (file type) = %02x %02x", rtprot[4], rtprot[5]);
	/* file id */
	if (read_tag(p, data_len, 0x83, &rtprot[6], 2) != 0)
		return -1;
	swap_pair(&rtprot[6], 2);
	DEBUG_INFO3("tag 0x83 (file id) = %02x %02x", rtprot[6], rtprot[7]);
	/* file size */
	if (read_tag(p, data_len, 0x81, &rtprot[0], 2) == 0) {
		swap_pair(&rtprot[0], 2);
		DEBUG_INFO3("tag 0x81 (complete file size) = %02x %02x",
				rtprot[0], rtprot[1]);
	}
	if (read_tag(p, data_len, 0x80, &rtprot[2], 2) == 0) {
		swap_pair(&rtprot[2], 2);
		DEBUG_INFO3("tag 0x80 (file size) = %02x %02x", rtprot[2], rtprot[3]);
	}
	if (read_tag(p, data_len, 0x86, secattr, sizeof(secattr)) == 0) {
		i = 17;
		memcpy(rtprot + i, secattr, 8);
		for (i += 8, p = &secattr[8]; i < sizeof(rtprot); ++i, p += 4)
			rtprot[i] = *p;
		DEBUG_INFO2("tag 0x86 = %s", ct_hexdump(&rtprot[17], 15));
	}
	memcpy(data, rtprot, sizeof(rtprot));
	return sizeof(rtprot);
}

int convert_rtprot_to_doinfo(void *data, size_t data_len)
{
	unsigned char doinfo[0xff] = { 0 };
	unsigned char *pdata = data;
	size_t i, doinfo_len = 0;

	if (data_len < 32) {
		DEBUG_INFO2("data_len = %u", data_len);
		return -1;
	}
	if (pdata[0] != 0 && pdata[0] < sizeof(doinfo) - 4 - 4 - 5 - 42 - 2) {
		/* Tag 0x80 */
		doinfo[doinfo_len++] = 0x80;
		doinfo[doinfo_len++] = 2;
		memcpy(doinfo + doinfo_len, pdata, 2);
		swap_pair(doinfo + doinfo_len, 2);
		doinfo_len += 2;
	}
	/* Tag 0x83 */
	doinfo[doinfo_len++] = 0x83;
	doinfo[doinfo_len++] = 2;
	doinfo[doinfo_len++] = pdata[2];
	doinfo[doinfo_len++] = pdata[3];

	/* Tag 0x85 */
	doinfo[doinfo_len++] = 0x85;
	doinfo[doinfo_len++] = 3;
	doinfo[doinfo_len++] = pdata[4];
	doinfo[doinfo_len++] = pdata[5];
	doinfo[doinfo_len++] = pdata[6];

	/* Tag 0x86 */
	doinfo[doinfo_len++] = 0x86;
	doinfo[doinfo_len++] = 40;
	memcpy(doinfo + doinfo_len, pdata + 17, 8);
	doinfo_len += 8;
	for (i = 0; i < 7 && doinfo_len + 3 < sizeof(doinfo); ++i, doinfo_len += 4)
		doinfo[doinfo_len] = pdata[17 + 8 + i];
	doinfo_len += 4; /* for reserved */
	if (pdata[0] != 0 && pdata[0] + doinfo_len + 2 < sizeof(doinfo)) {
		/* Tag 0xA5 */
		if (data_len - 32 < pdata[0]) {
			DEBUG_INFO2("for tag 0xA5 incorrect data_len = %u", data_len);
			return -1;
		}
		doinfo[doinfo_len++] = 0xA5;
		doinfo[doinfo_len++] = pdata[0];
		memcpy(doinfo + doinfo_len, pdata + 32, pdata[0]);
		doinfo_len += pdata[0];
	}
	DEBUG_INFO2("doinfo = %s", ct_hexdump(doinfo, doinfo_len));
	memcpy(data, doinfo, doinfo_len);
	return doinfo_len;
}

int convert_rtprot_to_fcp(void *data, size_t data_len)
{
	unsigned char fcp[63] = {
		0x62, sizeof(fcp) - 2,
		0x81, 2, 0, 0,
		0x80, 2, 0, 0,
		0x82, 2, 0, 0,
		0x83, 2, 0, 0,
		0x8A, 1, 0,
		0x86, 40
	};
	unsigned char *p = data;
	size_t i;

	if (data_len < sizeof(fcp)) {
		DEBUG_INFO2("data_len = %u", data_len);
		return -1;
	}
	/* Tag 0x81 */
	memcpy(fcp + 4, p, 2);
	swap_pair(fcp + 4, 2);
	/* Tag 0x80 */
	memcpy(fcp + 8, p + 2, 2);
	swap_pair(fcp + 8, 2);
	/* Tag 0x82 */
	memcpy(fcp + 12, p + 4, 2);
	/* Tag 0x83 */
	memcpy(fcp + 16, p + 6, 2);
	swap_pair(fcp + 16, 2);
	/* Tag 0x8A */
	fcp[20] = p[8];

	/* Tag 0x86 */
	memcpy(fcp + 23, p + 17, 8);
	for (i = 0; i < 7 && sizeof(fcp) > 23 + 8 + i * 4; ++i)
		fcp[23 + 8 + i * 4] = p[17 + 8 + i];
	DEBUG_INFO2("fcp = %s", ct_hexdump(fcp, sizeof(fcp)));
	memcpy(data, fcp, sizeof(fcp));
	return sizeof(fcp);
}