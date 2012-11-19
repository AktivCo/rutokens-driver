/*
 * APDU conversion for Rutoken S
 *
 *
 */
#ifndef CONVERT_APDU_H
#define CONVERT_APDU_H

#ifdef __cplusplus
extern "C" {
#endif

void swap_pair(unsigned char *buf, size_t len);
void swap_four(unsigned char *buf, size_t len);
int convert_doinfo_to_rtprot(void *data, size_t data_len);
int convert_fcp_to_rtprot(void *data, size_t data_len);
int convert_rtprot_to_doinfo(void *data, size_t data_len);
int convert_rtprot_to_fcp(void *data, size_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* CONVERT_APDU_H */
