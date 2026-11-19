// vis_dll.h
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*PFN_VIS_COMPUTE)(
	const unsigned char *in_bsp,
	int in_size,
	unsigned char **out_bsp,
	int *out_size,
	char *err_buf,
	int err_buf_size
);

typedef void (*PFN_VIS_FREE)(unsigned char *buf);

#ifdef __cplusplus
}
#endif